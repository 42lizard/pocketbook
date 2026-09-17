#include "mock_cloud.h"
#include "application.h"
#include <QTemporaryDir>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QUrl>
#include <sqlite3.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <iostream>
using namespace readest;
int main() {
    // Deliberately no Q(Core/Gui)Application, QML engine or display.
    QTemporaryDir root; assert(root.isValid());
    auto first=std::make_shared<MockCloud>(root.path()+"/one",READEST_SIM_FIXTURES);
    auto second=std::make_shared<MockCloud>(root.path()+"/two",READEST_SIM_FIXTURES);
    std::atomic<bool> cancel{false};
    const auto transport=first->transport();
    Cloud a((root.path()+"/a.json").toStdString(),"unused","public",
        "https://readest.supabase.co","https://web.readest.com",transport.bind_request(cancel));
    Cloud b((root.path()+"/b.json").toStdString(),"unused","public",
        "https://readest.supabase.co","https://web.readest.com",second->transport().bind_request(cancel));
    a.sign_in("test","test",time(nullptr)); b.sign_in("test","test",time(nullptr));
    const auto hash=first->firstHash();
    first->remoteChapter(hash,3);
    assert(a.get("/api/sync?type=configs&book="+hash.toStdString(),time(nullptr)).body.find("/6/6!")!=std::string::npos);
    assert(b.get("/api/sync?type=configs&book="+hash.toStdString(),time(nullptr)).body.find("/6/2!")!=std::string::npos);
    first->transfer(2);
    assert(transport.request("https://web.readest.com/api/sync?type=books","GET",{},"","",100000,cancel).status==503);
    assert(b.get("/api/sync?type=books",time(nullptr)).status==200);
    first->transfer(0);
    // Missing fixtures/origins fail closed without a live fallback.
    bool rejected=false;
    try { transport.request("https://example.com/","GET",{},"","",1000,cancel); }
    catch(const std::exception&) { rejected=true; }
    assert(rejected);
    rejected=false;
    try { transport.request("https://web.readest.com/unknown","GET",{},"","",1000,cancel); }
    catch(const std::exception&) { rejected=true; }
    assert(rejected);
    auto close_file=[](FILE* f) { fclose(f); };
    std::unique_ptr<FILE,decltype(close_file)> file(tmpfile(),close_file); assert(file);
    assert(transport.download("https://storage.simulator/missing",fileno(file.get()),"",1000,cancel).status==404);
    const auto object="simulator-user/Readest/Books/"+hash+"/book.epub";
    const auto url=("https://storage.simulator/"+QUrl::toPercentEncoding(object)).toStdString();
    // Cancellation is borrowed per call and cannot poison a later call or instance.
    for(bool download:{false,true}) {
        auto call=[&] {
            if(download) transport.download(url,fileno(file.get()),"",1000000,cancel);
            else transport.request("https://web.readest.com/api/sync?type=books","GET",{},"","",1000000,cancel);
        };
        cancel=true; rejected=false;
        try { call(); } catch(const std::exception& e) { rejected=std::string(e.what())=="Cancelled."; }
        assert(rejected);
        cancel=false; first->transfer(1);
        assert(ftruncate(fileno(file.get()),0)==0); rewind(file.get());
        bool received_bytes=false;
        std::thread stopper([&] {
            if(download) {
                for(int i=0;i<4000;++i) {
                    struct stat st{}; assert(fstat(fileno(file.get()),&st)==0);
                    if(st.st_size>0) { received_bytes=true; break; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } else std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cancel=true;
        });
        rejected=false;
        try { call(); } catch(const std::exception& e) { rejected=std::string(e.what())=="Cancelled."; }
        stopper.join(); assert(rejected && (!download || received_bytes));
        first->transfer(0);
        std::atomic<bool> next{false};
        assert(transport.download(url,fileno(file.get()),"",1000000,next).status==200);
        assert(second->transport().request("https://web.readest.com/api/sync?type=books","GET",{},"","",1000000,next).status==200);
        cancel=false;
    }
    // A new instance reloads persisted progress. Existing transport callbacks own
    // their instance, so releasing the overlay's handle cannot invalidate them.
    first.reset();
    auto restarted=std::make_shared<MockCloud>(root.path()+"/one",READEST_SIM_FIXTURES);
    assert(restarted->transport().request("https://web.readest.com/api/sync?type=configs&book="+hash.toStdString(),
        "GET",{},"","",1000000,cancel).body.find("/6/6!")!=std::string::npos);
    assert(transport.request("https://web.readest.com/api/sync?type=books","GET",{},"","",1000000,cancel).status==200);

    // Exercise the full application seam with the same Qt Core-only mock.
    restarted->remoteChapter(hash,1);
    const auto appRoot=root.path()+"/application";
    assert(QDir().mkpath(appRoot+"/state") && QDir().mkpath(appRoot+"/Books"));
    ApplicationConfig config;
    config.root=(appRoot+"/state").toStdString(); config.books_root=(appRoot+"/Books/Readest").toStdString();
    config.book_roots={appRoot.toStdString()}; config.database=(appRoot+"/native.db").toStdString();
    config.ca="unused"; config.public_key="public"; config.transport=restarted->transport();
    std::atomic<bool> download_started{false};
    const auto download=config.transport.download;
    config.transport.download=[&](const std::string& url,int fd,const std::string& ca,size_t cap,const std::atomic<bool>& token) {
        download_started=true;
        return download(url,fd,ca,cap,token);
    };
    sqlite3* db=nullptr; assert(sqlite3_open(config.database.c_str(),&db)==SQLITE_OK);
    auto sql=[&](const std::string& statement) { assert(sqlite3_exec(db,statement.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK); };
    sql("CREATE TABLE folders(id INTEGER PRIMARY KEY,storageid INTEGER,name TEXT); CREATE TABLE files(book_id INTEGER PRIMARY KEY,folder_id INTEGER,storageid INTEGER,filename TEXT,fast_hash BLOB);"
        "CREATE TABLE books_settings(bookid INTEGER,profileid INTEGER,position TEXT,position_ts INTEGER,cpage INTEGER,npage INTEGER,completed INTEGER);");
    ApplicationService application(config);
    assert(application.execute(Request{},cancel).outcome==Outcome::Ready);
    Request command; command.command=Command::SignIn; command.email="test"; command.password="test";
    assert(application.execute(command,cancel).outcome==Outcome::SignedIn);
    command.command=Command::Refresh;
    auto result=application.execute(command,cancel);
    assert(result.outcome==Outcome::Refreshed && result.library.books.size()==51);
    const BookId chosen{"simulator-user",hash.toStdString()}; command.book=chosen;
    command.command=Command::Covers; command.books={chosen};
    result=application.execute(command,cancel);
    assert(result.cover_updates.size()==1 && valid_cover(result.cover_updates.front().second));
    command.command=Command::Download;
    for(int fault:{2,3,4}) {
        restarted->transfer(fault);
        assert(application.execute(command,cancel).outcome==Outcome::Failed);
        assert(QDir(QString::fromStdString(config.books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
    }
    restarted->transfer(1); download_started=false;
    std::thread stopper([&] {
        for(int i=0;i<5000 && !download_started.load();++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); cancel=true;
    });
    result=application.execute(command,cancel); stopper.join();
    assert(download_started && result.outcome==Outcome::Cancelled);
    assert(QDir(QString::fromStdString(config.books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
    cancel=false; restarted->transfer(0);
    result=application.execute(command,cancel); assert(result.outcome==Outcome::Downloaded);
    std::string path;
    for(const auto& entry:result.library.books) if(entry.id==chosen) path=entry.book.path;
    assert(!path.empty());
    const QFileInfo book(QString::fromStdString(path));
    sql("INSERT INTO folders VALUES(1,1,'"+book.absolutePath().toStdString()+"');"
        "INSERT INTO files VALUES(1,1,1,'"+book.fileName().toStdString()+"',zeroblob(16));"
        "INSERT INTO books_settings VALUES(1,1,'pbr:/webkit?##epubcfi(/6/2!/4/2)',1,1,3,0);");
    command.command=Command::Sync; result=application.execute(command,cancel);
    if(result.outcome!=Outcome::Synced) std::cerr<<result.error<<"\n";
    assert(result.outcome==Outcome::Synced && result.sync_action==SyncAction::EstablishBaseline);
    sql("UPDATE books_settings SET position='pbr:/webkit?##epubcfi(/6/4!/4/2)',position_ts=2,cpage=2;");
    result=application.execute(command,cancel);
    assert(result.outcome==Outcome::Synced && result.sync_action==SyncAction::Upload);
    auto remote=restarted->transport().request("https://web.readest.com/api/sync?type=configs&book="+hash.toStdString(),
        "GET",{},"","",1000000,cancel);
    assert(remote.body.find("/6/4!")!=std::string::npos);
    // A device-only fixture goes through the real application upload boundary.
    const auto local=appRoot+"/PocketBook-only.epub";
    assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/51.epub",local));
    const auto localHash=inspect_epub(local.toStdString()).readest_hash;
    sql("INSERT INTO folders VALUES(2,1,'"+appRoot.toStdString()+"');"
        "INSERT INTO files VALUES(2,2,1,'PocketBook-only.epub',zeroblob(16));"
        "INSERT INTO books_settings VALUES(2,1,'pbr:/webkit?##epubcfi(/6/4!/4/2)',1,2,3,0);");
    command={};command.command=Command::Scan;result=application.execute(command,cancel);
    assert(result.library.books.size()==52);
    command.book={"simulator-user",localHash};command.command=Command::Upload;
    result=application.execute(command,cancel);
    if(result.outcome!=Outcome::Uploaded) std::cerr<<result.error<<" "<<result.progress_warning<<"\n";
    assert(result.outcome==Outcome::Uploaded);
    auto persisted=std::make_shared<MockCloud>(root.path()+"/one",READEST_SIM_FIXTURES);
    const auto uploaded=persisted->transport().request("https://web.readest.com/api/sync?type=books&book="+localHash,"GET",{},"","",1000000,cancel);
    assert(uploaded.body.find(localHash)!=std::string::npos);
    assert(sqlite3_close(db)==SQLITE_OK);
    std::cout<<"Independent headless mock cloud state and fault controls passed.\n";
}
