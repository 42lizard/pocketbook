#include "application.h"
#include "probe.h"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <cstring>
#include <sys/stat.h>
using namespace readest;
namespace readest { extern unsigned full_inspections; }
namespace {
const std::string alpha="epubcfi(/6/2!/4/2)",bravo="epubcfi(/6/4!/4/2)";
struct Faults {
    std::string database,audit,mode;
    std::atomic<bool>* cancel=nullptr;
    bool armed=false,committed=false;
} faults;
int commit(void*) {
    if(!faults.armed) return 0;
    faults.committed=true;
    if(faults.mode=="cancel" || faults.mode=="audit-cancel" || faults.mode=="state-failure-cancel") *faults.cancel=true;
    if(faults.mode=="audit" || faults.mode=="audit-cancel") assert(mkdir((faults.audit+"/committed.txt").c_str(),0700)==0);
    return faults.mode=="uncertain"?1:0;
}
int authorize(void* connection,int action,const char* table,const char*,const char*,const char*) {
    if(faults.armed && table && std::strcmp(table,"books_settings")==0) {
        const auto* path=sqlite3_db_filename(static_cast<sqlite3*>(connection),"main");
        if(faults.mode=="cancel-preparation" && action==SQLITE_READ && path && faults.database==path) *faults.cancel=true;
        if(faults.mode=="write-failure" && action==SQLITE_UPDATE) return SQLITE_DENY;
    }
    return faults.armed && faults.committed && faults.mode.starts_with("state-failure") && action==SQLITE_INSERT && table && std::strcmp(table,"sync")==0?SQLITE_DENY:SQLITE_OK;
}
// SQLite hooks fault the real connections; the native writer is never replaced.
int extension(sqlite3* db,char**,const sqlite3_api_routines*) {
    const char* path=sqlite3_db_filename(db,"main");
    if(path && faults.database==path) sqlite3_commit_hook(db,commit,nullptr);
    sqlite3_set_authorizer(db,authorize,db); return SQLITE_OK;
}
void sql(const std::string& path,const std::string& command) {
    sqlite3* db=nullptr; assert(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,command.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK); assert(sqlite3_close(db)==SQLITE_OK);
}
}
int main(int argc,char** argv) {
    assert(argc==3);
    const std::string root=std::string(argv[1])+"/resume"; make_directory(root);
    const std::string path=root+"/book.epub";
    std::filesystem::copy_file(argv[2],path);
    const auto bytes=inspect_epub(path);
    ManagedBook book; book.path=path; book.book.hash=bytes.readest_hash; book.sha256=bytes.sha256; book.size=bytes.size;
    const std::string database=root+"/native.db";
    sql(database,"CREATE TABLE folders(id,storageid,name); CREATE TABLE files(book_id,folder_id,storageid,filename,fast_hash);"
        "CREATE TABLE books_settings(bookid INTEGER,profileid INTEGER,position TEXT,position_ts INTEGER,cpage INTEGER,npage INTEGER,completed INTEGER);"
        "INSERT INTO folders VALUES(1,1,'"+root+"');"
        "INSERT INTO files VALUES(1,1,1,'book.epub',X'0123456789ABCDEF0123456789ABCDEF');"
        "INSERT INTO books_settings VALUES(1,1,'#"+alpha+"',100,1,100,0);");
    std::string remote=bravo;
    auto api=
        [&](const std::string& url,const std::string& method,const std::vector<std::string>&,const std::string&,const std::string&,size_t) {
            HttpResponse r; r.status=200;
            if(url.find("grant_type=password")!=std::string::npos)
                r.body=R"({"access_token":"dummy","refresh_token":"dummy-refresh","expires_at":9999999999,"user":{"id":"user"}})";
            else { assert(method=="GET"); r.body="{\"configs\":[{\"user_id\":\"user\",\"book_hash\":\""+book.book.hash+"\",\"location\":\""+remote+"\",\"updated_at\":1000000}]}"; }
            return r;
        };
    Cloud cloud(root+"/session.json","ca","public","https://auth.test","https://api.test",api);
    cloud.sign_in("test","dummy",1000);
    faults.database=database;
    assert(sqlite3_auto_extension(reinterpret_cast<void(*)()>(extension))==SQLITE_OK);
    State state(root+"/state.db"); SavedSync baseline;
    baseline.positions={alpha,alpha,alpha,alpha,true}; state.save_sync("user",book.book.hash,baseline);
    std::atomic<bool> cancel{false};
    NativeResumeContext context{database,root+"/success-audit","PB743G","U743g.6.11.1683"};
    const auto native=native_position(database,path);
    full_inspections=0;
    const VerifiedManagedBook verified(book);
    const auto staged=transition_progress(cloud,state,verified,native,1001,ProgressChoice::Automatic,0,false,context,cancel);
    assert(staged.outcome==ResumeOutcome::NotRequested && staged.action==SyncAction::ApplyRemote);
    assert(native_position(database,path).cfi==alpha && state.sync("user",book.book.hash).pending_remote==bravo);
    full_inspections=0; const VerifiedManagedBook open_verified(book);
    const auto opened=transition_progress(cloud,state,open_verified,native,1002,ProgressChoice::Automatic,staged.revision,true,context,cancel);
    const auto saved=state.sync("user",book.book.hash);
    assert(opened.outcome==ResumeOutcome::Applied && opened.revision==saved.revision);
    assert(saved.pending_remote.empty() && saved.positions.has_baseline);
    assert(saved.positions.local==bravo && saved.positions.last_local==bravo && saved.positions.last_remote==bravo);
    assert(native_position(database,path).cfi==bravo && full_inspections==1);
    assert(std::filesystem::exists(context.audit_directory+"/before.db"));
    for(const std::string mode:{"cancel","audit","audit-cancel","state-failure"}) {
        faults.armed=false; faults.committed=false; cancel=false;
        sql(database,"UPDATE books_settings SET position='#"+alpha+"',position_ts=100;");
        baseline.revision=state.sync("user",book.book.hash).revision; state.save_sync("user",book.book.hash,baseline);
        context.audit_directory=root+"/"+mode; faults.audit=context.audit_directory; faults.mode=mode; faults.cancel=&cancel;
        const auto observed=native_position(database,path);
        const auto stage=transition_progress(cloud,state,VerifiedManagedBook(book),observed,1003,ProgressChoice::Automatic,0,false,context,cancel);
        full_inspections=0; const VerifiedManagedBook checked(book);
        faults.armed=true;
        const auto outcome=transition_progress(cloud,state,checked,observed,1004,ProgressChoice::Automatic,stage.revision,true,context,cancel);
        faults.armed=false;
        assert(native_position(database,path).cfi==bravo && full_inspections==1);
        const auto persisted=state.sync("user",book.book.hash);
        if(mode=="state-failure") {
            assert(outcome.outcome==ResumeOutcome::AppliedUnrecorded && !outcome.error.empty());
            assert(persisted.pending_remote==bravo && persisted.positions.last_local==alpha);
        } else {
            assert(outcome.outcome==ResumeOutcome::Applied && persisted.pending_remote.empty());
            assert(outcome.revision==persisted.revision && persisted.positions.last_local==bravo);
            assert(outcome.warning.empty()==(mode=="cancel"));
        }
    }
    for(const std::string mode:{"cancel-before","cancel-preparation","stale-native","stale-revision","write-failure","unsafe-trigger","remote-change","uncertain","wrong-book","missing-settings"}) {
        faults.armed=false; faults.committed=false; cancel=false; remote=bravo;
        sql(database,"DROP TRIGGER IF EXISTS unexpected; DELETE FROM books_settings;"
            "INSERT INTO books_settings VALUES(1,1,'#"+alpha+"',100,1,100,0);");
        baseline.positions={alpha,alpha,alpha,alpha,true};
        if(mode=="missing-settings") {
            sql(database,"DELETE FROM books_settings;"); baseline.positions.local=""; baseline.positions.last_local="";
        }
        baseline.revision=state.sync("user",book.book.hash).revision; state.save_sync("user",book.book.hash,baseline);
        context.audit_directory=root+"/"+mode; faults.audit=context.audit_directory; faults.mode=mode;
        auto observed=native_position(database,path);
        const auto stage=transition_progress(cloud,state,VerifiedManagedBook(book),observed,1005,ProgressChoice::Automatic,0,false,context,cancel);
        if(mode=="cancel-before") cancel=true;
        if(mode=="stale-native") sql(database,"UPDATE books_settings SET position_ts=101;");
        if(mode=="unsafe-trigger") sql(database,"CREATE TRIGGER unexpected AFTER UPDATE ON books_settings BEGIN SELECT 1; END;");
        if(mode=="remote-change") remote=alpha;
        if(mode=="wrong-book") observed.book_path+="-other";
        faults.armed=true;
        bool rejected=false; ProgressTransition result;
        try {
            result=transition_progress(cloud,state,VerifiedManagedBook(book),observed,1006,
                mode=="stale-revision"?ProgressChoice::Readest:ProgressChoice::Automatic,
                mode=="stale-revision"?stage.revision-1:stage.revision,true,context,cancel);
        } catch(const std::exception&) { rejected=true; }
        faults.armed=false;
        assert(native_position(database,path).cfi==(mode=="missing-settings"?"":alpha));
        const auto persisted=state.sync("user",book.book.hash);
        if(mode=="stale-revision") assert(!rejected && result.outcome==ResumeOutcome::Blocked);
        else if(mode=="remote-change") assert(!rejected && result.outcome==ResumeOutcome::NoPending && persisted.pending_remote.empty());
        else if(mode=="uncertain") assert(!rejected && result.outcome==ResumeOutcome::CommitUncertain && !result.error.empty());
        else if(mode=="missing-settings") assert(!rejected && result.outcome==ResumeOutcome::NeedsNativeSettings);
        else assert(rejected);
        if(mode!="remote-change") assert(persisted.positions.last_local==(mode=="missing-settings"?"":alpha));
    }
    // Public application results must suppress handoff on cancellation or a
    // committed-but-unrecorded result, even when cancellation accompanies failure.
    for(const std::string mode:{"success","cancel","state-failure","state-failure-cancel","uncertain"}) {
        faults.armed=false; faults.committed=false; cancel=false; remote=bravo;
        sql(database,"DELETE FROM books_settings; INSERT INTO books_settings VALUES(1,1,'#"+alpha+"',100,1,100,0);");
        ApplicationConfig config; config.root=root+"/app-"+mode; config.books_root=config.root+"/Books/Readest";
        config.database=database; config.model=context.model; config.firmware=context.firmware;
        config.ca="ca"; config.public_key="public"; config.auth_origin="https://auth.test"; config.api_origin="https://api.test";
        config.transport.request=[&](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
            const std::string& body,const std::string& ca,size_t cap,const std::atomic<bool>&) { return api(url,method,headers,body,ca,cap); };
        config.transport.download=[](const std::string&,int,const std::string&,size_t,const std::atomic<bool>&)->HttpResponse { throw std::runtime_error("Unexpected download"); };
        make_directory(config.root);
        State app_state(config.root+"/state.db"); LibraryPage page; page.cursor=1; page.books={book.book};
        app_state.apply_page("user",0,page); app_state.register_download("user",book.book.hash,{path,bytes});
        SavedSync initial; initial.positions={alpha,alpha,alpha,alpha,true}; app_state.save_sync("user",book.book.hash,initial);
        ApplicationService service(config); assert(service.execute(Request{},cancel).library.initialized);
        Request request; request.command=Command::SignIn; request.email="test"; request.password="test";
        assert(service.execute(request,cancel).outcome==Outcome::SignedIn);
        request={}; request.command=Command::Sync; request.book={"user",book.book.hash};
        const auto stage=service.execute(request,cancel); assert(stage.sync_action==SyncAction::ApplyRemote);
        request.command=Command::Open; request.revision=stage.revision; faults.mode=mode; faults.armed=true;
        full_inspections=0; const auto result=service.execute(request,cancel); faults.armed=false;
        assert(full_inspections==1);
        if(mode=="success") assert(result.outcome==Outcome::Applied && result.open_path==path);
        else {
            assert(result.open_path.empty());
            if(mode=="cancel") assert(result.outcome==Outcome::Cancelled && app_state.sync("user",book.book.hash).pending_remote.empty());
            if(mode.starts_with("state-failure")) assert(result.outcome==Outcome::AppliedUnrecorded && !result.error.empty());
            if(mode=="uncertain") assert(result.outcome==Outcome::NativeCommitUncertain && !result.error.empty());
        }
        if(mode.starts_with("state-failure")) {
            cancel=false; request.command=Command::Sync;
            const auto recovered=service.execute(request,cancel);
            assert(recovered.outcome==Outcome::Synced && recovered.sync_action==SyncAction::EstablishBaseline);
            assert(app_state.sync("user",book.book.hash).pending_remote.empty());
            assert(native_position(database,path).cfi==bravo);
        }
    }
    sqlite3_reset_auto_extension();
    std::cout<<"Native resume transition checks passed.\n";
}
