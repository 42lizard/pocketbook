#include "download.h"
#include "json_util.h"
#include "state.h"
#include <cassert>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
using namespace readest;
static std::string read(const std::string& path) {
    std::ifstream f(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {});
}
static size_t entries(const std::string& path) {
    DIR* d = opendir(path.c_str()); assert(d); size_t n = 0;
    while (auto* e = readdir(d)) if (e->d_name[0] != '.') ++n;
    closedir(d); return n;
}
int main(int argc, char** argv) {
    assert(argc == 3);
    std::string root = std::string(argv[1]) + "/managed", bytes = read(argv[2]);
    assert(mkdir(root.c_str(), 0700) == 0);
    const std::string hash = "81fbcb860e2eed5d223c359063680f87";
    int mode = 0, downloads = 0, signed_requests = 0, api_requests=0;
    auto api = [&](const std::string& url, const std::string&, const std::vector<std::string>&,
                   const std::string&, const std::string&, size_t) {
        ++api_requests; HttpResponse r; r.status = 200;
        if (url.find("grant_type=password") != std::string::npos)
            r.body = R"({"access_token":"dummy-access","refresh_token":"dummy-refresh","expires_at":9999999999,"user":{"id":"fixture-user"}})";
        else if (url.find("/api/storage/list?") != std::string::npos) {
            assert(url.find("bookHash=" + hash) != std::string::npos);
            std::string file = "{\"file_key\":\"fixture-user/Readest/Books/" + hash +
                "/A & B.epub\",\"book_hash\":\"" + hash + "\",\"file_size\":" + std::to_string(bytes.size()) + "}";
            r.body = "{\"page\":1,\"totalPages\":1,\"files\":[" + (mode == 5 ? "" : file) + "]}";
        } else {
            assert(url.find("/api/storage/download?fileKey=fixture-user%2FReadest%2FBooks%2F") != std::string::npos);
            assert(url.find("A%20%26%20B.epub") != std::string::npos || url.find("cover.png")!=std::string::npos);
            if(mode==6) { r.status=404; return r; }
            ++signed_requests; r.body = R"({"downloadUrl":"https://storage.test/object?signature=dummy"})";
        }
        return r;
    };
    Cloud cloud(std::string(argv[1]) + "/download-session.json", "test-ca", "public-test", "https://auth.test", "https://api.test", api);
    cloud.sign_in("test", "dummy", 1000);
    LibraryBook book; book.hash = hash; book.format = "EPUB"; book.title = "../A & B";
    auto transfer = [&](const std::string& url, int fd, const std::string& ca, size_t cap) {
        assert(url == "https://storage.test/object?signature=dummy" && ca == "test-ca" && cap == bytes.size());
        ++downloads; HttpResponse r; r.status = 200;
        if (mode == 1) throw std::runtime_error("interrupted");
        if (mode == 2 && downloads == 1) { r.status = 403; return r; }
        std::string payload = bytes;
        if (mode == 3) payload.resize(payload.size() - 10);
        if (mode == 4) payload[0] = 'x';
        assert(write(fd, payload.data(), payload.size()) == static_cast<ssize_t>(payload.size()));
        r.bytes = payload.size(); return r;
    };
    auto first = download_book(cloud, book, root, "test-ca", 1000, transfer);
    assert(read(first.path) == bytes && entries(root) == 1);
    auto meta = parse_json(read(first.path.substr(0, first.path.rfind('/')) + "/readest.json"));
    assert(string_member(meta.get(), "bookHash") == hash && string_member(meta.get(), "userId") == "fixture-user");
    assert(string_member(meta.get(), "sha256") == first.integrity.sha256);
    const std::string state_path = std::string(argv[1]) + "/state.db";
    {
        State state(state_path);
        assert(recover_downloads(state, "other-user", root).empty());
        assert(state.books("other-user").empty());
        assert(recover_downloads(state, "fixture-user", root).empty());
        assert(state.books("fixture-user").size() == 1);
        assert(state.books("fixture-user")[0].path == first.path);
        LibraryPage page; page.cursor = 123; book.title = "Cloud title"; page.books.push_back(book);
        state.apply_page("fixture-user", 0, page);
        assert(state.cursor("fixture-user") == 123 && state.books("fixture-user")[0].book.title == "Cloud title");
        bool failed = false;
        try { state.apply_page("fixture-user", 0, page); } catch (const std::runtime_error&) { failed = true; }
        assert(failed && state.cursor("fixture-user") == 123);
        // A rejected later row must roll back earlier metadata and the cursor.
        LibraryPage invalid = page; invalid.cursor = 125;
        invalid.books[0].title = "Must roll back";
        LibraryBook bad; bad.hash = "invalid"; invalid.books.push_back(bad);
        failed = false;
        try { state.apply_page("fixture-user", 123, invalid); } catch(const std::runtime_error&) { failed = true; }
        assert(failed && state.cursor("fixture-user") == 123 && state.books("fixture-user")[0].book.title == "Cloud title");
        page.cursor = 124; page.books[0].deleted = true;
        state.apply_page("fixture-user", 123, page);
        assert(state.books("fixture-user")[0].book.deleted && read(first.path) == bytes);
        SavedSync checkpoint;
        checkpoint.positions.local = "epubcfi(/6/2!/4/2)";
        checkpoint.pending_remote = "epubcfi(/6/4!/4/2)";
        checkpoint.remote_config = "{\"unrelated\":true}";
        state.save_sync("fixture-user", hash, checkpoint);
        failed = false;
        try { state.save_sync("fixture-user", hash, checkpoint); } catch(const std::runtime_error&) { failed = true; }
        assert(failed);
    }
    {
        State restored(state_path);
        assert(restored.cursor("fixture-user") == 124);
        assert(restored.books("fixture-user")[0].path == first.path);
        auto saved = restored.sync("fixture-user", hash);
        assert(saved.revision == 1 && saved.pending_remote == "epubcfi(/6/4!/4/2)" && saved.remote_config == "{\"unrelated\":true}");
        assert(restored.sync("other-user", hash).revision == 0);
        assert(recover_downloads(restored, "fixture-user", root).empty());
    }
    for (int failure : {1, 3, 4, 5}) {
        mode = failure; bool failed = false;
        try { download_book(cloud, book, root, "test-ca", 1000, transfer); }
        catch (const std::runtime_error&) { failed = true; }
        assert(failed && entries(root) == 1 && read(first.path) == bytes);
    }
    mode = 2; downloads = signed_requests = 0;
    auto second = download_book(cloud, book, root, "test-ca", 1000, transfer);
    assert(downloads == 2 && signed_requests == 2 && entries(root) == 2);
    assert(second.path != first.path && read(first.path) == bytes && read(second.path) == bytes);
    State restored(state_path);
    assert(recover_downloads(restored, "fixture-user", root).size() == 1); // Duplicate is retained, never silently substituted.
    assert(restored.books("fixture-user")[0].path == first.path);
    const auto moved=first.path+".moved";
    assert(rename(first.path.c_str(),moved.c_str())==0);
    assert(restored.books("fixture-user")[0].path.empty());
    assert(recover_downloads(restored,"fixture-user",root).empty());
    assert(restored.books("fixture-user")[0].path==second.path);
    assert(read(moved)==bytes && restored.sync("fixture-user",hash).revision==0);
    assert(unlink(second.path.c_str())==0);
    assert(restored.books("fixture-user")[0].path.empty());
    mode=0;
    auto replacement=download_book(cloud,book,root,"test-ca",1000,transfer);
    restored.register_download("fixture-user",hash,replacement);
    assert(restored.books("fixture-user")[0].path==replacement.path);
    assert(read(replacement.path)==bytes && read(moved)==bytes);
    // A dangling symlink is unsafe, not a missing download to replace.
    assert(unlink(replacement.path.c_str())==0);
    assert(symlink("absent",replacement.path.c_str())==0);
    assert(!missing_download(replacement.path));
    bool rejected=false;
    try { restored.register_download("fixture-user",hash,first); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected);
    assert(restored.books("fixture-user")[0].epubs==-1);
    BookFiles assets; assets.epubs=1; assets.cover_key="fixture-user/Readest/Books/"+hash+"/cover.png";
    assets.cover_size=24; assets.cover_stamp=1000;
    restored.save_book_files("fixture-user",{{hash,assets}});
    { State reopened(state_path); assert(reopened.books("fixture-user")[0].epubs==1); }
    restored.save_book_files("other-user",{});
    assert(restored.books("fixture-user")[0].epubs==1);
    restored.save_book_files("fixture-user",{});
    assert(restored.books("fixture-user")[0].epubs==0);
    // Bound encoded image size and decoded dimensions before handing it to InkView.
    unsigned char png[24]={137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,1,0,0,0,1};
    int images=0;
    auto image_transfer=[&](const std::string& url,int fd,const std::string&,size_t cap) {
        assert(url.find("https://storage.test/")==0 && cap==24); ++images;
        assert(write(fd,png,sizeof(png))==sizeof(png));
        HttpResponse response; response.status=200; response.bytes=24; return response;
    };
    cache_cover(cloud,root,hash,assets,"test-ca",1000,image_transfer);
    const auto image=cover_path(root,"fixture-user",hash,assets);
    assert(valid_cover(image));
    cache_cover(cloud,root,hash,assets,"test-ca",1000,image_transfer); assert(images==1);
    BookFiles unlisted;
    auto unknown_transfer=[&](const std::string&,int fd,const std::string&,size_t cap) {
        assert(cap==2*1024*1024);
        assert(write(fd,png,sizeof(png))==sizeof(png));
        HttpResponse r; r.status=200; r.bytes=sizeof(png); return r;
    };
    mode=6;
    assert(!cache_cover(cloud,root,hash,unlisted,"test-ca",1000,unknown_transfer));
    mode=0;
    assert(cache_cover(cloud,root,hash,unlisted,"test-ca",1000,unknown_transfer));
    assert(valid_cover(cover_path(root,"fixture-user",hash,unlisted)));
    assets.cover_stamp=1001; png[16]=127;
    rejected=false;
    try { cache_cover(cloud,root,hash,assets,"test-ca",1000,image_transfer); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected && !valid_cover(cover_path(root,"fixture-user",hash,assets)));
    assert(valid_cover(image));
    // JPEG bytes stored under Readest's cover.png name, including progressive SOF.
    unsigned char jpeg[]={255,216,255,224,0,4,0,0,255,194,0,11,8,0,1,0,1,1,1,17,0,255,217};
    assets.cover_size=sizeof(jpeg); assets.cover_stamp=1002;
    auto jpeg_transfer=[&](const std::string&,int fd,const std::string&,size_t cap) {
        assert(cap==sizeof(jpeg)); assert(write(fd,jpeg,sizeof(jpeg))==sizeof(jpeg));
        HttpResponse response; response.status=200; response.bytes=sizeof(jpeg); return response;
    };
    cache_cover(cloud,root,hash,assets,"test-ca",1000,jpeg_transfer);
    assert(cover_format(cover_path(root,"fixture-user",hash,assets))==CoverFormat::JPEG);
    jpeg[15]=0x7f; assets.cover_stamp=1003;
    rejected=false;
    try { cache_cover(cloud,root,hash,assets,"test-ca",1000,jpeg_transfer); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); // Oversized JPEG is rejected before native decoding.
    jpeg[15]=0; jpeg[10]=0x7f; assets.cover_stamp=1004;
    rejected=false;
    try { cache_cover(cloud,root,hash,assets,"test-ca",1000,jpeg_transfer); }
    catch(const std::runtime_error&) { rejected=true; }
    assert(rejected); // Truncated marker segment cannot be accepted.

    // Human-readable names remain safe paths and retain exact EPUB bytes.
    mode = 0;
    struct NamingCase { std::string title, author, expected; };
    const NamingCase names[] = {
        {"The Left Hand of Darkness", "Ursula K. Le Guin", "The Left Hand of Darkness - Ursula K. Le Guin.epub"},
        {"Über das Lesen 日本語", "José García", "Über das Lesen 日本語 - José García.epub"},
        {"../A & B", "", "A & B.epub"},
        {" A: \"Book\"/Part\\Two?*<>|\n. ", " Author\tName ", "A Book Part Two - Author Name.epub"},
        {"...", " / ", "Untitled.epub"},
        {"con", "", "_con.epub"},
        {"LPT1.notes", "", "_LPT1.notes.epub"},
        {std::string(119, 'a') + "日末", std::string(59, 'b') + "é", std::string(119, 'a') + " - " + std::string(59, 'b') + ".epub"}
    };
    for (const auto& name : names) {
        book.title = name.title; book.author = name.author;
        const auto saved = download_book(cloud, book, root, "test-ca", 1000, transfer);
        assert(saved.path.substr(saved.path.rfind('/') + 1) == name.expected);
        assert(read(saved.path) == bytes);
        const auto metadata = parse_json(read(saved.path.substr(0, saved.path.rfind('/')) + "/readest.json"));
        assert(string_member(metadata.get(), "filename") == name.expected);
        const auto duplicate = download_book(cloud, book, root, "test-ca", 1000, transfer);
        assert(duplicate.path != saved.path && read(saved.path) == bytes);
    }

    // Discover ordinary device books, including cloud entries with progress only.
    const auto device = std::string(argv[1]) + "/device";
    assert(mkdir(device.c_str(), 0700) == 0);
    for (const auto& dir : {"system", ".hidden", "managed", "Novels"})
        assert(mkdir((device + "/" + dir).c_str(), 0700) == 0);
    auto write_book = [&](const std::string& path) {
        std::ofstream file(path, std::ios::binary); file << bytes; assert(file.good());
    };
    for (const auto& dir : {"system", ".hidden", "managed"}) write_book(device + "/" + dir + "/book.epub");
    assert(symlink(argv[2], (device + "/linked.epub").c_str()) == 0);
    { std::ofstream invalid(device + "/broken.epub"); invalid << "not a ZIP"; }
    State discovered(std::string(argv[1]) + "/discovered.db");
    LibraryPage page; page.cursor = 1; page.books.push_back(book);
    LibraryBook unrelated = book; unrelated.hash = std::string(32, '0'); page.books.push_back(unrelated);
    discovered.apply_page("fixture-user", 0, page);
    BookFiles progress_only; progress_only.epubs = 0;
    discovered.save_book_files("fixture-user", {{hash, progress_only}});
    const auto scan = [&] { return discover_device_books(discovered, "fixture-user",
        {device, device + "/absent-sd-card"}, device + "/managed", [] { return false; }); };
    assert(scan() == 0);
    const auto local = device + "/Novels/My existing book.EPUB";
    write_book(local);
    bool cancelled = false;
    try { discover_device_books(discovered, "fixture-user", {device}, device + "/managed", [] { return true; }); }
    catch (const std::runtime_error&) { cancelled = true; }
    assert(cancelled);
    assert(scan() == 1 && scan() == 0 && read(local) == bytes);
    assert(discovered.books("other-user").empty());
    for (const auto& found : discovered.books("fixture-user")) {
        if (found.book.hash == hash) {
            assert(found.path == local && found.sha256 == first.integrity.sha256);
            assert(found.epubs == 0 && book_availability(found) == Availability::OnDevice);
        } else assert(found.path.empty());
    }
    const auto moved_local = device + "/Moved.epub";
    assert(rename(local.c_str(), moved_local.c_str()) == 0);
    assert(scan() == 1 && read(moved_local) == bytes);
    ManagedBook category; category.book.format = "EPUB";
    assert(book_availability(category) == Availability::Unknown);
    category.epubs = 0; assert(book_availability(category) == Availability::ProgressOnly);
    category.epubs = 1; assert(book_availability(category) == Availability::Downloadable);
    category.epubs = 2; assert(book_availability(category) == Availability::Multiple);
    category.book.format = "PDF"; assert(book_availability(category) == Availability::Unavailable);
    category.book.deleted = true; assert(book_availability(category) == Availability::Removed);
    // Missing covers are not queried once per refresh forever. A new cover version or TTL retries.
    mode=6; BookFiles missing_cover;
    const auto now=std::time(nullptr); const auto calls=api_requests;
    const std::string absent_hash(32,'c');
    assert(!cache_cover(cloud,root,absent_hash,missing_cover,"test-ca",now,unknown_transfer));
    assert(!cache_cover(cloud,root,absent_hash,missing_cover,"test-ca",now,unknown_transfer));
    assert(api_requests==calls+1);
    missing_cover.cover_stamp=1;
    assert(!cache_cover(cloud,root,absent_hash,missing_cover,"test-ca",now,unknown_transfer));
    assert(api_requests==calls+2);
    assert(!cache_cover(cloud,root,absent_hash,missing_cover,"test-ca",now+6*60*60+1,unknown_transfer));
    assert(api_requests==calls+3);

}
