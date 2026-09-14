#include "application.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>
using namespace readest;
namespace readest {
extern unsigned full_inspections;
// This headless test must never fall through to network I/O.
HttpResponse https_request(const std::string&,const std::string&,const std::vector<std::string>&,
    const std::string&,const std::string&,size_t) { throw std::runtime_error("Unexpected network request"); }
HttpResponse https_download(const std::string&,int,const std::string&,size_t) { throw std::runtime_error("Unexpected download"); }
}
int main(int argc,char** argv) {
    assert(argc==3);
    const auto base=std::string(argv[1])+"/application"; make_directory(base);
    ApplicationConfig config; config.root=base+"/state"; config.books_root=base+"/Books/Readest";
    config.database=base+"/absent-native.db"; config.ca="fixture-ca"; config.public_key="public";
    config.book_roots={base}; make_directory(config.root);
    const auto original=inspect_epub(argv[2]);
    LibraryBook book; book.hash=original.readest_hash; book.title="Existing book"; book.format="EPUB";
    {
        State state(config.root+"/state.db"); LibraryPage page; page.cursor=1; page.books.push_back(book);
        state.apply_page("fixture-user",0,page);
        std::ofstream bad(config.root+"/session.json"); bad<<"{bad";
    }
    int requests=0; bool fail_progress=false;
    config.transport=[&](const std::string& url,const std::string&,const std::vector<std::string>&,
        const std::string&,const std::string&,size_t) {
        ++requests;
        if(url.find("type=configs")!=std::string::npos) {
            HttpResponse r; r.status=fail_progress?503:200; r.body=R"({"configs":[]})"; return r;
        }
        if(url.find("/api/sync?")!=std::string::npos) { HttpResponse r; r.status=200; r.body=R"({"books":[]})"; return r; }
        if(url.find("/api/storage/list?")!=std::string::npos) { HttpResponse r; r.status=200; r.body=R"({"page":1,"totalPages":1,"files":[]})"; return r; }
        assert(url.find("grant_type=password")!=std::string::npos);
        HttpResponse response; response.status=200;
        response.body=R"({"access_token":"dummy-access","refresh_token":"dummy-refresh","expires_at":9999999999,"user":{"id":"fixture-user"}})";
        return response;
    };
    ApplicationService service(config); std::atomic<bool> cancel{false};
    auto result=service.execute(Request{},cancel);
    assert(result.outcome==Outcome::SessionInvalid && result.library.initialized && !result.library.signed_in);
    Request request; request.command=Command::SignIn; request.email="test"; request.password="test";
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SignedIn && result.library.books.size()==1 && requests==1);
    // A late copy is reused by Download itself; no storage request or copy occurs.
    const auto local=base+"/My existing book.epub";
    { std::ifstream input(argv[2],std::ios::binary); std::ofstream output(local,std::ios::binary); output<<input.rdbuf(); }
    request={}; request.command=Command::Download; request.book={"fixture-user",book.hash};
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Reused && requests==1);
    assert(result.library.books[0].book.path==local && result.library.books[0].availability==Availability::OnDevice);
    request.command=Command::ReadOffline; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::LocalOpen && result.open_path==local && requests==1);
    // Provide an empty native library: the valid EPUB has no saved position yet.
    sqlite3* native=nullptr;
    assert(sqlite3_open(config.database.c_str(),&native)==SQLITE_OK);
    assert(sqlite3_exec(native,"CREATE TABLE files(book_id,folder_id,storageid,filename,fast_hash);"
        "CREATE TABLE folders(id,storageid,name);"
        "CREATE TABLE books_settings(bookid,profileid,position,position_ts,cpage,npage);",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(native)==SQLITE_OK);
    for(const auto command:{Command::Sync,Command::Open,Command::ReadOffline}) {
        request.command=command; full_inspections=0;
        result=service.execute(request,cancel);
        assert(result.outcome==(command==Command::ReadOffline?Outcome::LocalOpen:Outcome::Synced));
        assert(full_inspections==1);
        assert(result.open_path==(command==Command::Sync?std::string():local));
    }
    fail_progress=true; request.command=Command::Open; full_inspections=0;
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SyncUnavailable && result.open_path==local && full_inspections==1);
    fail_progress=false; request.command=Command::ReadOffline;
    State standalone_state(config.root+"/state.db");
    Cloud standalone_cloud(config.root+"/session.json",config.ca,config.public_key,
        "https://auth.test","https://api.test",config.transport);
    standalone_cloud.load_session();
    const auto managed=standalone_state.books("fixture-user").at(0);
    full_inspections=0;
    sync_managed(standalone_cloud,standalone_state,managed,"",time(nullptr));
    assert(full_inspections==1);
    request.book.account="another-user"; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Failed && result.open_path.empty());
    request.book.account="fixture-user"; cancel=true; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Cancelled && result.open_path.empty()); cancel=false;
    // Read preparation catches changes rather than handing an unverified path to the device.
    { std::ofstream output(local,std::ios::binary|std::ios::app); output<<"changed"; }
    const auto before_changed=requests;
    for(const auto command:{Command::Sync,Command::Open,Command::ReadOffline}) {
        request.command=command; full_inspections=0;
        result=service.execute(request,cancel);
        assert(result.outcome==Outcome::Failed && result.open_path.empty() && full_inspections==1);
        assert(requests==before_changed);
    }
    // Standalone callers must retain validation, too.
    full_inspections=0; bool rejected=false;
    try { sync_managed(standalone_cloud,standalone_state,managed,"",time(nullptr)); }
    catch(const std::exception&) { rejected=true; }
    assert(rejected && full_inspections==1);
    // Metadata refresh must not perform a per-book cover request.
    const auto before_refresh=requests;
    request.command=Command::Refresh; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Refreshed && requests==before_refresh+2);
    request.command=Command::SignOut; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SignedOut && !result.library.signed_in && result.library.books.empty());
    assert(access(local.c_str(),F_OK)==0 && requests==before_refresh+2);
    std::cout<<"Headless application session recovery, identity, EPUB reuse, offline open and cancellation checks passed.\n";
}
