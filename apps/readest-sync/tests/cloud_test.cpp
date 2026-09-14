#include "cloud.h"
#include "json_util.h"
#include "library.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace readest;
struct Request { std::string url, method, body; std::vector<std::string> headers; };
std::string token(const char* access, const char* refresh, long long expires, const char* user = "fixture-user") {
    return std::string("{\"access_token\":\"") + access + "\",\"refresh_token\":\"" + refresh +
        "\",\"expires_at\":" + std::to_string(expires) + ",\"user\":{\"id\":\"" + user + "\"}}";
}
std::string read_file(const std::string& path) {
    std::ifstream f(path); return std::string(std::istreambuf_iterator<char>(f), {});
}
template<class F> void fails(F f) {
    bool failed = false; try { f(); } catch (const std::runtime_error&) { failed = true; }
    assert(failed);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string path = std::string(argv[1]) + "/session.json";
    std::vector<Request> requests;
    std::vector<HttpResponse> responses;
    auto transport = [&](const std::string& url, const std::string& method,
                         const std::vector<std::string>& headers, const std::string& body,
                         const std::string& ca, size_t limit) {
        assert(ca == "fixture-ca" && limit <= 4 * 1024 * 1024);
        if (url.compare(0, 16, "https://api.test/") == 0) {
            auto saved = parse_json(read_file(path));
            assert(headers[0] == "Authorization: Bearer " + string_member(saved.get(), "access_token"));
        }
        requests.push_back({url, method, body, headers});
        assert(!responses.empty());
        auto response = responses.front(); responses.erase(responses.begin());
        if(response.body.size()>limit) throw std::runtime_error("Fixture exceeds transport response limit");
        return response;
    };
    auto reply = [&](long status, const std::string& body) {
        HttpResponse r; r.status = status; r.body = body; responses.push_back(r);
    };
    Cloud client(path, "fixture-ca", "public-fixture-key", "https://auth.test", "https://api.test", transport);
    client.load_session(); assert(!client.session().signed_in());
    fails([&] { client.get("/api/sync?since=0", 1000); });
    assert(requests.empty());
    reply(200, token("access-1", "refresh-1", 1100));
    client.sign_in("test@example.invalid", "password-with-quote\"", 1000);
    assert(requests.back().url == "https://auth.test/auth/v1/token?grant_type=password");
    auto login = parse_json(requests.back().body);
    assert(string_member(login.get(), "password") == "password-with-quote\"");
    assert(client.session().access_token == "access-1");
    assert(read_file(path).find("password") == std::string::npos);
    struct stat st; assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    Cloud restored(path, "fixture-ca", "public-fixture-key", "https://auth.test", "https://api.test", transport);
    restored.load_session(); assert(restored.session().refresh_token == "refresh-1");

    // Expiry refresh must be persisted before the authenticated GET is sent.
    reply(200, token("access-2", "refresh-2", 2100));
    reply(200, "{\"books\":[]}");
    client.get("/api/sync?since=0&type=books", 1050);
    assert(requests[1].url.find("grant_type=refresh_token") != std::string::npos);
    assert(requests[2].headers[0] == "Authorization: Bearer access-2");
    restored.load_session(); assert(restored.session().refresh_token == "refresh-2");
    // An early 401 refreshes/retries exactly once; a second 401 is returned.
    reply(401, "{}"); reply(200, token("access-3", "refresh-3", 3100)); reply(401, "{}");
    auto result = client.get("/api/sync?since=0", 1100);
    assert(result.status == 401 && responses.empty());
    assert(requests.back().headers[0] == "Authorization: Bearer access-3");
    const auto before = read_file(path);
    // No silent account switch, no replacement on failed refresh.
    reply(200, token("wrong", "wrong", 4100, "other-user"));
    fails([&] { client.refresh(1200); }); assert(read_file(path) == before);
    reply(503, "server-error-secret");
    fails([&] { client.refresh(1200); }); assert(read_file(path) == before);
    reply(200, "{\"access_token\":\"broken\"}");
    fails([&] { client.refresh(1200); }); assert(read_file(path) == before);
    size_t count = requests.size();
    fails([&] { client.get("https://other.test/api/sync", 1200); });
    fails([&] { client.post("/api/sync", "{}garbage", 1200); });
    assert(requests.size() == count);
    const std::string books = R"({"books":[{"user_id":"fixture-user","book_hash":"81fbcb860e2eed5d223c359063680f87","title":"Fixture","format":"EPUB","author":null,"updated_at":"2099-01-01T00:00:00Z","synced_at":"2026-09-09T12:00:00.123456+00:00","deleted_at":null},{"user_id":"fixture-user","book_hash":"00000000000000000000000000000000","title":"Deleted","format":"EPUB","synced_at":"2026-09-09T12:00:00.123456+00:00","deleted_at":"2026-09-09T11:00:00Z"}]})";
    for(const auto& stamp : {"2026-09-09T12:00:00.123456+00", "2026-09-09T12:00:00.123456+0000",
            "2026-09-09 12:00:00.123456+00:00", "2026-09-09T14:00:00.123456+02:00",
            "2026-09-09T06:30:00.123456-0530", "2026-09-09T12:00:00.123456Z"}) {
        auto row=parse_json(std::string("{\"time\":\"")+stamp+"\"}");
        assert(record_timestamp(row.get(),"time")==1788955200123LL);
    }
    for(const auto& stamp : {"2026-09-09T12:00:00", "2026-09-09T12:00:00+24:00",
            "2026-09-09T12:00:00+02:60", "2026-09-09T12:00:00+0x", "2026-09-09T12:00:00+00junk"}) {
        auto row=parse_json(std::string("{\"time\":\"")+stamp+"\"}");
        fails([&] { record_timestamp(row.get(),"time"); });
    }
    auto page = parse_library_page(books, "fixture-user", 0, 1);
    assert(page.books.size() == 2 && page.more);
    assert(page.cursor == 1788955200123LL); // synced_at, not future updated_at.
    assert(page.books[0].author.empty() && page.books[1].deleted);
    assert(page.books[0].raw.find("2099") != std::string::npos);
    fails([&] { parse_library_page(books, "other-user", 0, 1); });
    fails([&] { parse_library_page(books, "fixture-user", page.cursor, 1); });
    fails([&] { parse_library_page("{}", "fixture-user", 0, 1); });
    auto empty = parse_library_page("{\"books\":[]}", "fixture-user", 456, 50);
    assert(empty.books.empty() && !empty.more && empty.cursor == 456);
    reply(200, books);
    auto fetched = fetch_library_page(client, 0, 50, 1200);
    assert(fetched.books.size() == 2 && !fetched.more);
    assert(requests.back().url == "https://api.test/api/sync?type=books&since=0&limit=50");
    auto boundary_row=[](int n,const std::string& stamp) {
        char hash[33]; std::snprintf(hash,sizeof(hash),"%032x",n);
        return std::string("{\"user_id\":\"fixture-user\",\"book_hash\":\"")+hash+"\",\"synced_at\":\""+stamp+"\"}";
    };
    std::string boundary="{\"books\":[";
    for(int i=1;i<=100;++i) { if(i>1) boundary+=","; boundary+=boundary_row(i,"2026-09-09T12:00:00.123456+00:00"); }
    boundary+="]}";
    auto first_boundary=parse_library_page(boundary,"fixture-user",0,100);
    const auto edge=first_boundary.cursor;
    const auto remainder=boundary.substr(0,boundary.size()-2)+","+
        boundary_row(101,"2026-09-09T12:00:00.123900+00:00")+","+
        boundary_row(102,"2026-09-09T12:00:00.124100+00:00")+"]}";
    reply(200,boundary); reply(200,remainder);
    auto recovered=fetch_library_page(client,edge,100,1200);
    assert(recovered.books.size()==102 && recovered.cursor==edge+1 && !recovered.more);
    assert(requests.back().url=="https://api.test/api/sync?type=books&since="+std::to_string(edge)+"&limit=200");
    // When only the fractional boundary remains, finish without inventing +1.
    reply(200,boundary); reply(200,boundary);
    recovered=fetch_library_page(client,edge,100,1200);
    assert(recovered.books.size()==100 && recovered.cursor==edge && !recovered.more);
    reply(200,boundary); reply(503,"{}");
    fails([&] { fetch_library_page(client,edge,100,1200); });

    // The production limited endpoint includes the entire trailing exact tie.
    const auto complete=boundary.substr(0,boundary.size()-2)+","+
        boundary_row(101,"2026-09-09T12:00:00.123456+00:00")+"]}";
    reply(200,complete);
    recovered=fetch_library_page(client,edge-1,100,1200);
    assert(recovered.books.size()==101 && recovered.cursor==edge && recovered.more);
    assert(requests.back().url=="https://api.test/api/sync?type=books&since="+std::to_string(edge-1)+"&limit=100");
    auto aligned=complete;
    for(size_t at=0;(at=aligned.find("123456",at))!=std::string::npos;at+=6) aligned.replace(at,6,"123000");
    reply(200,aligned);
    recovered=fetch_library_page(client,edge-1,100,1200);
    assert(recovered.books.size()==101 && recovered.cursor==edge && recovered.more);
    reply(200,"{\"books\":[]}");
    recovered=fetch_library_page(client,edge,100,1200);
    assert(recovered.books.empty() && !recovered.more);

    // A 6 MB delta must be consumed as two bounded 3 MB pages, not one fallback.
    auto large_page=[&](int first) {
        std::string result="{\"books\":[";
        for(int i=first;i<first+100;++i) {
            if(i>first) result+=",";
            auto row=boundary_row(i,"2026-09-09T12:00:01.000Z");
            row.pop_back();
            row+=",\"metadata\":\""+std::string(30000,'x')+"\",\"updated_at\":0}";
            result+=row;
        }
        return result+"]}";
    };
    auto large1=large_page(1),large2=large_page(101);
    // Distinct page boundaries, as returned by the production ascending query.
    for(size_t at=0;(at=large2.find("12:00:01",at))!=std::string::npos;at+=8) large2.replace(at,8,"12:00:02");
    assert(large1.size()<4*1024*1024 && large2.size()<4*1024*1024 && large1.size()+large2.size()>4*1024*1024);
    reply(200,large1); reply(200,large2); reply(200,"{\"books\":[]}");
    auto large=fetch_library_page(client,0,100,1200);
    assert(large.books.size()==100 && large.more);
    large=fetch_library_page(client,large.cursor,100,1200);
    assert(large.books.size()==100 && large.more && large.books.front().hash=="00000000000000000000000000000065");
    large=fetch_library_page(client,large.cursor,100,1200);
    assert(large.books.empty() && !large.more);
    assert(requests.back().url.find("&limit=100")!=std::string::npos);
    std::string dense="{\"books\":[";
    for(int i=1;i<=1000;++i) {
        if(i>1) dense+=",";
        dense+=boundary_row(i,"2026-09-09T12:00:00.123456Z");
    }
    dense+="]}";
    reply(200,dense);
    fails([&] { fetch_library_page(client,edge,100,1200); });
    const std::string hash="81fbcb860e2eed5d223c359063680f87";
    const std::string epub="{\"book_hash\":\""+hash+"\",\"file_key\":\"user/"+hash+"/book.epub\",\"file_size\":100}";
    const std::string cover="{\"book_hash\":\""+hash+"\",\"file_key\":\"user/"+hash+"/cover.png\",\"file_size\":80,\"updated_at\":123}";
    reply(200,"{\"page\":1,\"totalPages\":2,\"files\":["+epub+","+cover+"]}");
    reply(200,"{\"page\":2,\"totalPages\":2,\"files\":["+epub+",{\"book_hash\":null}]}");
    auto files=fetch_book_files(client,1200);
    assert(files.size()==1 && files.at(hash).epubs==1 && files.at(hash).cover_stamp==123);
    assert(files.at(hash).cover_key=="user/"+hash+"/cover.png");
    assert(requests.back().url=="https://api.test/api/storage/list?page=2&pageSize=1000");
    reply(200,"{\"page\":1,\"totalPages\":1,\"files\":["+cover+"]}");
    assert(fetch_book_files(client,1200).at(hash).epubs==0);
    auto invalid_cover=cover; invalid_cover.replace(invalid_cover.find("123"),3,"\"invalid-zone\"");
    reply(200,"{\"page\":1,\"totalPages\":1,\"files\":["+epub+","+invalid_cover+"]}");
    files=fetch_book_files(client,1200);
    assert(files.at(hash).epubs==1 && files.at(hash).cover_key.empty());
    reply(200,"{\"page\":1,\"totalPages\":2,\"files\":["+epub+"]}"); reply(503,"{}");
    fails([&] { fetch_book_files(client,1200); }); // No partial result may mark missing files.
    reply(200,"{\"page\":7,\"totalPages\":7,\"files\":[]}");
    fails([&] { fetch_book_files(client,1200); });
    count = requests.size();
    client.sign_out(); restored.load_session();
    assert(!client.session().signed_in() && !restored.session().signed_in());
    assert(requests.size() == count); // Does not log out iPhone/KOReader.
    for(const auto& broken : {std::string("{"), std::string("[]"), std::string("{\"access_token\":\"incomplete\"}")}) {
        { std::ofstream output(path); output<<broken; }
        fails([&] { client.load_session(); });
        assert(!client.session().signed_in() && read_file(path)==broken);
        reply(200,token("recovered","recovered-refresh",4100));
        client.sign_in("test@example.invalid","password",1200);
        restored.load_session(); assert(restored.session().refresh_token=="recovered-refresh");
    }
    // Refuse symlink storage rather than reading another file.
    std::string alias = std::string(argv[1]) + "/alias.json";
    assert(symlink(path.c_str(), alias.c_str()) == 0);
    Cloud unsafe(alias, "fixture-ca", "public-fixture-key", "https://auth.test", "https://api.test", transport);
    fails([&] { unsafe.load_session(); });
    fails([&] { unsafe.sign_out(); });
    // Failed token persistence must prevent the subsequent API request.
    Cloud missing(std::string(argv[1]) + "/absent/session.json", "fixture-ca", "public-fixture-key",
                  "https://auth.test", "https://api.test", transport);
    reply(200, token("unused", "unused", 4100));
    fails([&] { missing.sign_in("a", "b", 1200); });
    assert(!missing.session().signed_in());
    assert(responses.empty());
}
