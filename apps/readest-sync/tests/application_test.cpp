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
}
int main(int argc,char** argv) {
    assert(argc==3);
    const auto base=std::string(argv[1])+"/application"; make_directory(base);
    {
        State state(base+"/delta.db"); sqlite3* audit=nullptr;
        assert(sqlite3_open((base+"/delta.db").c_str(),&audit)==SQLITE_OK);
        assert(sqlite3_exec(audit,"CREATE TABLE events(kind);"
            "CREATE TRIGGER inserted AFTER INSERT ON local_copies BEGIN INSERT INTO events VALUES('insert'); END;"
            "CREATE TRIGGER updated AFTER UPDATE ON local_copies BEGIN INSERT INTO events VALUES('update'); END;"
            "CREATE TRIGGER deleted AFTER DELETE ON local_copies BEGIN INSERT INTO events VALUES('delete'); END;",
            nullptr,nullptr,nullptr)==SQLITE_OK);
        auto counts=[&](int inserts,int updates,int deletes) {
            sqlite3_stmt* q=nullptr;
            assert(sqlite3_prepare_v2(audit,"SELECT COUNT(*) FROM events WHERE kind=?",-1,&q,nullptr)==SQLITE_OK);
            for(const auto& entry:std::vector<std::pair<const char*,int>>{{"insert",inserts},{"update",updates},{"delete",deletes}}) {
                sqlite3_bind_text(q,1,entry.first,-1,SQLITE_STATIC);
                assert(sqlite3_step(q)==SQLITE_ROW && sqlite3_column_int(q,0)==entry.second);
                sqlite3_reset(q);
            }
            sqlite3_finalize(q);
            assert(sqlite3_exec(audit,"DELETE FROM events",nullptr,nullptr,nullptr)==SQLITE_OK);
        };
        LocalCopy a,b,c; a.path="a"; b.path="b"; c.path="c";
        state.replace_local_copies({a,b}); counts(2,0,0);
        state.replace_local_copies({b,a}); counts(0,0,0);
        a.position="new position";
        state.replace_local_copies({a,b,c}); counts(1,1,0);
        state.replace_local_copies({a,c}); counts(0,0,1);
        a.stamp="changed"; a.hash="new hash"; a.title="new title"; a.author="new author"; a.size=42;
        state.replace_local_copies({a,c}); counts(0,1,0);
        const auto saved=state.local_copies().at(0);
        assert(saved.stamp==a.stamp && saved.hash==a.hash && saved.title==a.title && saved.author==a.author && saved.size==a.size);
        state.replace_local_copies({}); counts(0,0,2);
        state.replace_local_copies({}); counts(0,0,0);
        assert(sqlite3_close(audit)==SQLITE_OK);
    }
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
    int requests=0,annotation_gets=0; bool fail_progress=false;
    const std::atomic<bool>* expected_cancel=nullptr;
    config.transport.request=[&](const std::string& url,const std::string&,const std::vector<std::string>&,
        const std::string&,const std::string&,size_t,const std::atomic<bool>& token) {
        assert(&token==expected_cancel);
        ++requests;
        if(url.find("type=notes")!=std::string::npos) {++annotation_gets;return HttpResponse{200,R"({"notes":[]})",0};}
        if(url.find("type=configs")!=std::string::npos) {
            HttpResponse r; r.status=fail_progress?503:200; r.body=R"({"configs":[]})"; return r;
        }
        if(url.find("/api/sync?")!=std::string::npos) { HttpResponse r; r.status=200; r.body=R"({"books":[]})"; return r; }
        if(url.find("/api/storage/list?")!=std::string::npos) { HttpResponse r; r.status=200; r.body=R"({"page":1,"totalPages":1,"files":[]})"; return r; }
        if(url.find("/api/storage/download?")!=std::string::npos) { HttpResponse r; r.status=404; return r; }
        assert(url.find("grant_type=password")!=std::string::npos);
        HttpResponse response; response.status=200;
        response.body=R"({"access_token":"dummy-access","refresh_token":"dummy-refresh","expires_at":9999999999,"user":{"id":"fixture-user"}})";
        return response;
    };
    config.transport.download=[](const std::string&,int,const std::string&,size_t,const std::atomic<bool>&) -> HttpResponse {
        throw std::runtime_error("Unexpected download");
    };
    ApplicationService service(config); std::atomic<bool> cancel{false}; expected_cancel=&cancel;
    auto result=service.execute(Request{},cancel);
    assert(result.outcome==Outcome::SessionInvalid && result.library.initialized && !result.library.signed_in);
    Request request; request.command=Command::SignIn; request.email="test"; request.password="test";
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SignedIn && result.library.books.size()==1 && requests==1);
    // A late copy is reused by Download itself; no storage request or copy occurs.
    const auto local=base+"/My existing book.epub";
    { std::ifstream input(argv[2],std::ios::binary); std::ofstream output(local,std::ios::binary); output<<input.rdbuf(); }
    // Native indexed copy is reusable without a download; no saved position yet.
    sqlite3* native=nullptr;
    assert(sqlite3_open(config.database.c_str(),&native)==SQLITE_OK);
    assert(sqlite3_exec(native,"CREATE TABLE files(book_id,folder_id,storageid,filename,fast_hash);"
        "CREATE TABLE folders(id,storageid,name);"
        "CREATE TABLE books_settings(bookid,profileid,position,position_ts,cpage,npage);",nullptr,nullptr,nullptr)==SQLITE_OK);
    const auto indexed="INSERT INTO folders VALUES(1,1,'"+base+"');INSERT INTO files VALUES(1,1,1,'My existing book.epub',X'00112233445566778899AABBCCDDEEFF');";
    assert(sqlite3_exec(native,indexed.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(native)==SQLITE_OK);
    request={}; request.command=Command::Download; request.book={"fixture-user",book.hash};
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Reused && requests==1);
    assert(result.library.books[0].book.path==local && result.library.books[0].availability==Availability::OnDevice);
    // A warm scan must not rewrite an unchanged library on flash storage.
    sqlite3* scan_state=nullptr;
    assert(sqlite3_open((config.root+"/state.db").c_str(),&scan_state)==SQLITE_OK);
    assert(sqlite3_exec(scan_state,"CREATE TRIGGER unchanged_scan BEFORE DELETE ON local_copies "
        "BEGIN SELECT RAISE(ABORT,'Unchanged scan rewrote the library'); END;",nullptr,nullptr,nullptr)==SQLITE_OK);
    request.command=Command::Scan; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::Scanned);
    assert(sqlite3_exec(scan_state,"DROP TRIGGER unchanged_scan",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(scan_state)==SQLITE_OK);
    // The fast path must still persist a changed native reading position.
    assert(sqlite3_open(config.database.c_str(),&native)==SQLITE_OK);
    assert(sqlite3_exec(native,"INSERT INTO books_settings(bookid,profileid,position) VALUES(1,0,'changed-position')",nullptr,nullptr,nullptr)==SQLITE_OK);
    result=service.execute(request,cancel); assert(result.outcome==Outcome::Scanned);
    { State saved(config.root+"/state.db"); assert(saved.local_copies().at(0).position=="changed-position"); }
    assert(sqlite3_exec(native,"DELETE FROM books_settings",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(native)==SQLITE_OK);
    request.command=Command::ReadOffline; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::LocalOpen && result.open_path==local && requests==1);
    for(const auto command:{Command::Sync,Command::Open,Command::ReadOffline}) {
        request.command=command; full_inspections=0;
        result=service.execute(request,cancel);
        assert(result.outcome==(command==Command::ReadOffline?Outcome::LocalOpen:Outcome::Synced));
        assert(full_inspections==1);
        assert(result.open_path==(command==Command::Sync?std::string():local));
    }
    {
        auto annotated=config;annotated.annotations_database=base+"/annotations.db";
        annotated.model="PB743G";annotated.firmware="U743g.6.11.1683";
        sqlite3* notes=nullptr;assert(sqlite3_open(annotated.annotations_database.c_str(),&notes)==SQLITE_OK);
        assert(sqlite3_exec(notes,"CREATE TABLE TypeNames(OID,TypeName);CREATE TABLE TagNames(OID,TagName);"
            "CREATE TABLE Items(OID,ParentID,TypeID,State,TimeAlt,HashUUID);CREATE TABLE Tags(ItemID,TagID,Val,TimeEdt);"
            "INSERT INTO TypeNames VALUES(0,'type.book');INSERT INTO Items VALUES(1,NULL,0,0,0,'00112233445566778899AABBCCDDEEFF');",
            nullptr,nullptr,nullptr)==SQLITE_OK);sqlite3_close(notes);
        ApplicationService with_annotations(annotated);assert(with_annotations.execute(Request{},cancel).outcome==Outcome::Ready);
        request.command=Command::Sync;auto annotated_result=with_annotations.execute(request,cancel);
        assert(annotated_result.outcome==Outcome::Synced && annotated_result.progress_warning.empty() && annotation_gets==1);
        request.command=Command::Open;annotated_result=with_annotations.execute(request,cancel);
        assert(annotated_result.open_path==local && annotated_result.progress_warning.empty() && annotation_gets==2);
        request.command=Command::ReadOffline;with_annotations.execute(request,cancel);assert(annotation_gets==2);
        assert(unlink(annotated.annotations_database.c_str())==0);
        request.command=Command::Open;annotated_result=with_annotations.execute(request,cancel);
        assert(annotated_result.open_path==local && annotated_result.progress_warning.find("Annotations:")!=std::string::npos);
    }
    fail_progress=true; request.command=Command::Open; full_inspections=0;
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SyncUnavailable && result.open_path==local && full_inspections==1);
    fail_progress=false; request.command=Command::ReadOffline;
    State standalone_state(config.root+"/state.db");
    Cloud standalone_cloud(config.root+"/session.json",config.ca,config.public_key,
        "https://auth.test","https://api.test",config.transport.bind_request(cancel));
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
    // A fresh operation token replaces the previous one, including after failure.
    std::atomic<bool> next_cancel{false}; expected_cancel=&next_cancel;
    request.command=Command::Refresh; result=service.execute(request,next_cancel);
    assert(result.outcome==Outcome::Refreshed);
    expected_cancel=&cancel;
    request={}; request.command=Command::Covers; request.books={{"fixture-user",book.hash}};
    result=service.execute(request,cancel);
    assert(result.cover_updates.empty() && result.cover_attempts==request.books);
    cancel=true; result=service.execute(request,cancel); cancel=false;
    assert(result.outcome==Outcome::Cancelled && result.cover_attempts.empty());
    request.command=Command::SignOut; result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SignedOut && !result.library.signed_in && result.library.books.size()==1);
    assert(access(local.c_str(),F_OK)==0 && requests==before_refresh+5);
    config.transport.download={};
    bool incomplete_rejected=false;
    try { ApplicationService incomplete(config); } catch(const std::invalid_argument&) { incomplete_rejected=true; }
    assert(incomplete_rejected);
    std::cout<<"Headless application session recovery, identity, EPUB reuse, offline open and cancellation checks passed.\n";
}
