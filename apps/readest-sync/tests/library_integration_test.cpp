#include "application.h"
#include "json_util.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
using namespace readest;
namespace fs=std::filesystem;
int main(int argc,char** argv) {
    assert(argc==3);
    const std::string base=std::string(argv[1])+"/integration";
    fs::create_directories(base+"/books");
    const auto path=base+"/books/Local.epub";
    fs::copy_file(argv[2],path);
    fs::copy_file(argv[2],base+"/unindexed.epub");
    ApplicationConfig config; config.root=base+"/state"; config.books_root=base+"/books/Readest";
    config.public_key="public"; config.ca="fixture-ca";
    config.database=base+"/native.db"; config.book_roots={base+"/books"};
    sqlite3* db=nullptr; assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    const auto schema="CREATE TABLE files(book_id,folder_id,storageid,filename,fast_hash);"
        "CREATE TABLE folders(id,storageid,name);"
        "CREATE TABLE books_settings(bookid INTEGER,profileid INTEGER,position TEXT,position_ts INTEGER,cpage INTEGER,npage INTEGER);"
        "INSERT INTO folders VALUES(1,1,'"+base+"/books');"
        "INSERT INTO files VALUES(1,1,1,'Local.epub',X'00112233445566778899AABBCCDDEEFF');";
    assert(sqlite3_exec(db,schema.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    int requests=0,uploads=0; bool fail_position=true,fail_upload=false,fail_metadata=false; std::string remote_book,remote_config;
    const auto hash=epub_fingerprint(path);
    std::string auth_account="fixture-user";
    config.transport.request=[&](const std::string& url,const std::string& method,const auto&,const std::string& body,const auto&,size_t,const auto&) -> HttpResponse {
        ++requests;
        if(url.find("grant_type=password")!=std::string::npos) return {200,"{\"access_token\":\"token\",\"refresh_token\":\"refresh\",\"expires_at\":9999999999,\"user\":{\"id\":\""+auth_account+"\"}}",0};
        if(url.find("/api/storage/upload")!=std::string::npos) return {200,R"({"uploadUrl":"https://storage.test/book"})",0};
        if(url.find("type=books")!=std::string::npos) return {200,"{\"books\":["+remote_book+"]}",0};
        if(url.find("type=configs")!=std::string::npos) return {200,"{\"configs\":["+remote_config+"]}",0};
        if(url.find("/api/sync")!=std::string::npos && method=="POST") {
            auto json=parse_json(body); auto* books=member(json.get(),"books");
            if(json_object_array_length(books)) {
                if(fail_metadata) return {503,"{}",0};
                auto* book=json_object_array_get_idx(books,0);
                remote_book="{\"user_id\":\"fixture-user\",\"book_hash\":\""+hash+"\",\"title\":\"Uploaded\",\"format\":\"EPUB\",\"updated_at\":1}";
                assert(string_member(book,"hash")==hash);
            } else {
                if(fail_position) return {503,"{}",0};
                auto* value=json_object_array_get_idx(member(json.get(),"configs"),0);
                remote_config="{\"user_id\":\"fixture-user\",\"book_hash\":\""+hash+"\",\"updated_at\":"+std::to_string(integer_member(value,"updatedAt"))+",\"location\":"+json_text(member(value,"location"))+"}";
            }
            return {200,"{}",0};
        }
        throw std::runtime_error("Unexpected request: "+url);
    };
    config.transport.upload=[&](const auto&,int,const auto&,size_t size,const auto&) -> HttpResponse {
        assert(size==fs::file_size(path)); ++uploads; if(fail_upload) throw std::runtime_error("Interrupted upload"); return {200,"",0};
    };
    config.transport.download=[](const auto&,int,const auto&,size_t,const auto&) -> HttpResponse { throw std::runtime_error("No download expected"); };
    std::atomic<bool> cancel{false};
    ApplicationService service(config);
    auto result=service.execute({},cancel);
    assert(result.library.initialized && !result.library.signed_in);
    assert(result.library.books.size()==1);
    assert(result.library.books[0].book.path==path);
    assert(result.library.books[0].availability==Availability::OnDevice);
    Request request; request.command=Command::ReadOffline; request.book=result.library.books[0].id;
    result=service.execute(request,cancel);
    assert(result.outcome==Outcome::LocalOpen && result.open_path==path && requests==0);
    // Newly uploaded book keeps a pending position across service restart.
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"INSERT INTO books_settings VALUES(1,0,'epubcfi(/6/2!/4/2/1:0)',1,9,100)",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    request={};request.command=Command::SignIn;request.email="test";request.password="test";
    result=service.execute(request,cancel); assert(result.outcome==Outcome::SignedIn);
    request={};request.command=Command::Upload;request.book=result.library.books[0].id;
    result=service.execute(request,cancel);
    std::cerr<<"Upload outcome "<<static_cast<int>(result.outcome)<<" error="<<result.error<<" warning="<<result.progress_warning<<" uploads="<<uploads<<"\n";
    assert(result.outcome==Outcome::UploadPending && uploads==1 && !remote_book.empty());
    request.command=Command::Open;result=service.execute(request,cancel);
    assert(result.outcome==Outcome::SyncUnavailable && result.library.books[0].upload_pending);
    request.command=Command::Upload;
    ApplicationService restarted(config); result=restarted.execute({},cancel);
    assert(result.library.books.size()==1 && result.library.books[0].upload_pending);
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"UPDATE books_settings SET position='epubcfi(/6/4!/4/2/1:0)',cpage=40",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    fail_position=false; result=restarted.execute(request,cancel);
    assert(result.outcome==Outcome::Uploaded && uploads==1 && !remote_config.empty());
    assert(string_member(parse_json(remote_config).get(),"location")=="epubcfi(/6/4!/4/2/1:0)");
    assert(!result.library.books[0].upload_pending && !result.library.books[0].book.local_only);
    request.command=Command::SignOut;result=restarted.execute(request,cancel);
    assert(!result.library.signed_in && result.library.books.size()==1);
    // Different local copies require an explicit selection, retained on restart.
    fs::copy_file(path,base+"/books/Copy.epub");
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"INSERT INTO files VALUES(2,1,1,'Copy.epub',X'00112233445566778899AABBCCDDEEFF');INSERT INTO books_settings VALUES(2,0,'epubcfi(/6/6!/4/2/1:0)',1,40,100)",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    request.command=Command::Scan;result=restarted.execute(request,cancel);
    assert(result.library.books.size()==1 && result.library.books[0].book.needs_copy_choice);
    request.book=result.library.books[0].id;request.command=Command::ReadOffline;
    assert(restarted.execute(request,cancel).outcome==Outcome::Failed);
    request.command=Command::SelectCopy;request.local_path=path;result=restarted.execute(request,cancel);
    assert(result.outcome==Outcome::CopySelected && !result.library.books[0].book.needs_copy_choice);
    ApplicationService selected(config);result=selected.execute({},cancel);
    assert(!result.library.books[0].book.needs_copy_choice && result.library.books[0].book.path==path);
    fs::remove(base+"/books/Copy.epub");
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"DELETE FROM books_settings",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    // A reservation is not a completed upload; a restart never retries on its own.
    config.root=base+"/retry-state";remote_book.clear();remote_config.clear();fail_upload=true;
    ApplicationService interrupted(config);result=interrupted.execute({},cancel);
    request={};request.command=Command::SignIn;request.email="test";request.password="test";
    result=interrupted.execute(request,cancel);request={};request.book=result.library.books[0].id;request.command=Command::Upload;
    int before=uploads;result=interrupted.execute(request,cancel);
    assert(result.outcome==Outcome::Failed && uploads==before+1 && remote_book.empty());
    const auto pending_request=request;
    request.command=Command::SignOut;interrupted.execute(request,cancel);
    auth_account="second-user";request.command=Command::SignIn;request.email="other";request.password="test";
    result=interrupted.execute(request,cancel);
    assert(result.library.account=="second-user" && !result.library.books[0].upload_pending);
    assert(interrupted.execute(pending_request,cancel).outcome==Outcome::Failed && uploads==before+1);
    request.command=Command::SignOut;interrupted.execute(request,cancel);auth_account="fixture-user";
    request.command=Command::SignIn;result=interrupted.execute(request,cancel);
    assert(result.library.books[0].upload_pending);request=pending_request;
    ApplicationService retry(config);result=retry.execute({},cancel);
    assert(result.library.books[0].upload_pending && uploads==before+1);
    fail_upload=false;fail_metadata=true;result=retry.execute(request,cancel);
    assert(result.outcome==Outcome::Failed && uploads==before+2 && remote_book.empty());
    ApplicationService publish_retry(config);result=publish_retry.execute({},cancel);
    assert(result.library.books[0].upload_pending && uploads==before+2);
    fail_metadata=false;result=publish_retry.execute(request,cancel);
    assert(result.outcome==Outcome::Uploaded && uploads==before+2 && remote_config.empty());
    // Pending work cannot be invoked with another account identity.
    request.book.account="another-user";
    assert(publish_retry.execute(request,cancel).outcome==Outcome::Failed && uploads==before+2);
    // Existing cloud progress is reconciled without publishing duplicate metadata or bytes.
    config.root=base+"/existing-state";
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"INSERT INTO books_settings VALUES(1,0,'epubcfi(/6/2!/4/2/1:0)',1,9,100)",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    remote_config="{\"user_id\":\"fixture-user\",\"book_hash\":\""+hash+"\",\"updated_at\":1,\"location\":\"epubcfi(/6/4!/4/2/1:0)\"}";
    ApplicationService existing(config);existing.execute({},cancel);
    request={};request.command=Command::SignIn;request.email="test";request.password="test";
    result=existing.execute(request,cancel);request={};request.book=result.library.books[0].id;request.command=Command::Upload;
    before=uploads;const auto prior_remote=remote_config;result=existing.execute(request,cancel);
    assert(result.outcome==Outcome::UploadPending && result.sync_action==SyncAction::Conflict && uploads==before && remote_config==prior_remote);
    // Unsupported native progress reports a warning; the book still uploads.
    config.root=base+"/unsupported-state";remote_book.clear();remote_config.clear();
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    assert(sqlite3_exec(db,"UPDATE books_settings SET position='unsupported native location'",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(db)==SQLITE_OK);
    ApplicationService unsupported(config);unsupported.execute({},cancel);
    request={};request.command=Command::SignIn;request.email="test";request.password="test";
    result=unsupported.execute(request,cancel);request={};request.book=result.library.books[0].id;request.command=Command::Upload;
    result=unsupported.execute(request,cancel);
    assert(result.outcome==Outcome::Uploaded && !result.progress_warning.empty() && remote_config.empty());
    // Hundreds of indexed copies remain one book; unindexed files stay excluded.
    assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    for(int i=3;i<303;++i) {
        const auto name="Scale-"+std::to_string(i)+".epub";fs::create_hard_link(path,base+"/books/"+name);
        const auto sql="INSERT INTO files VALUES("+std::to_string(i)+",1,1,'"+name+"',X'00112233445566778899AABBCCDDEEFF');";
        assert(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);
    }
    assert(sqlite3_close(db)==SQLITE_OK);
    request={};request.command=Command::Scan;result=selected.execute(request,cancel);
    assert(result.library.books.size()==1 && result.library.books[0].book.copies.size()==301);
    result=selected.execute(request,cancel);assert(result.library.books[0].book.copies.size()==301);
    for(int i=3;i<303;++i) fs::remove(base+"/books/Scale-"+std::to_string(i)+".epub");
    fs::remove(path); request.command=Command::Scan;
    result=restarted.execute(request,cancel);
    assert(result.outcome==Outcome::Scanned && result.library.books.empty());
    ApplicationConfig broken=config;broken.root=base+"/unindexed.epub";
    ApplicationService unavailable(broken);result=unavailable.execute({},cancel);
    assert(result.outcome==Outcome::Failed && !result.library.initialized && !result.error.empty());
    std::cout<<"Indexed local library and signed-out opening passed.\n";
}
