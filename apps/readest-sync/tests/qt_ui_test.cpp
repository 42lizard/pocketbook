// Link the production controller/model/runner normally; test public contracts.
#include "controller.h"
#include "book_status.h"
#include "cover_provider.h"
#include "network_route.h"
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QStyleHints>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cassert>
#include <iostream>
using namespace readest;
namespace platform_test {
extern std::atomic<bool> hold_ping,ping_entered,ping_finished;
extern std::atomic<int> ping_calls;
extern std::atomic<bool> sleeping_wifi;
extern std::atomic<int> wake_calls;
}
static void settle(int milliseconds=100) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static void finish(AppController& control) {
    QElapsedTimer timer; timer.start();
    while(control.busy() && timer.elapsed()<5000) settle(10);
    assert(!control.busy());
}
static int count_named(QQuickItem* item,const QString& name) {
    int count=item->objectName()==name;
    for(auto* child:item->childItems()) count+=count_named(child,name);
    return count;
}
static QQuickItem* find_item(QQuickItem* item,const QString& name) {
    if(item->objectName()==name) return item;
    for(auto* child:item->childItems()) if(auto* found=find_item(child,name)) return found;
    return nullptr;
}
static void tap_item(QQuickWindow* window,QQuickItem* item,int hold=0) {
    assert(item);
    const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    QMouseEvent press(QEvent::MouseButtonPress,point,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&press);
    if(hold) settle(hold);
    QMouseEvent release(QEvent::MouseButtonRelease,point,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&release); settle();
}
static void tap_book(QQuickWindow* window,int hold=0) {
    tap_item(window,find_item(window->contentItem(),"detailedBookRow"),hold);
}
static ApplicationConfig configAt(const QString& base) {
    ApplicationConfig config;
    config.root=(base+"/system/readest-sync").toStdString();
    config.books_root=(base+"/Books/Readest").toStdString();
    config.database=(base+"/system/explorer-3/explorer-3.db").toStdString();
    config.ca="test-ca"; config.public_key="public"; config.book_roots={base.toStdString()};
    config.auth_origin="https://auth.test"; config.api_origin="https://api.test";
    config.transport.request=[](const std::string& url,const std::string&,const std::vector<std::string>&,const std::string&,const std::string&,size_t,const std::atomic<bool>&) {
        HttpResponse r; r.status=500; r.body="{}";
        if(url.find("grant_type=password")!=std::string::npos) {
            r.status=200;
            r.body=R"({"access_token":"test-access","refresh_token":"test-refresh","expires_at":9999999999,"user":{"id":"fixture-user"}})";
        }
        return r;
    };
    config.transport.download=[](const std::string&,int,const std::string&,size_t,const std::atomic<bool>&) -> HttpResponse { throw std::runtime_error("Unexpected download"); };
    assert(QDir().mkpath(QString::fromStdString(config.root)));
    return config;
}
static void bookStatusChecks() {
    struct Scenario {
        Availability availability;
        int epubs;
        bool deleted,upload_pending,local_only;
        const char* expected;
    };
    const Scenario scenarios[]={
        {Availability::OnDevice,0,true,true,false,"Removed from Readest"},
        {Availability::OnDevice,0,true,false,false,"Removed from Readest"},
        {Availability::OnDevice,0,false,true,false,"Upload pending"},
        {Availability::OnDevice,0,false,true,true,"Upload pending"},
        {Availability::OnDevice,0,false,false,true,"PocketBook only"},
        {Availability::OnDevice,0,false,false,false,"On device · Readest progress only"},
        {Availability::OnDevice,1,false,false,false,"On device"},
        {Availability::Downloadable,1,false,false,false,"Available to download"},
        {Availability::ProgressOnly,0,false,false,false,"Progress only"},
        {Availability::Unknown,0,false,false,false,"Not checked"},
        {Availability::Unavailable,0,false,false,false,"EPUB unavailable"},
        {Availability::Multiple,2,false,false,false,"Multiple EPUBs"},
        {Availability::Removed,0,false,false,false,"Removed from cloud"},
        {Availability::Removed,0,true,false,false,"Removed from Readest"}
    };
    QTemporaryDir temp; assert(temp.isValid());
    AppController control(configAt(temp.path()),DeviceAccess{});
    assert(control.book().isEmpty() && control.hint().isEmpty());
    for(const auto& scenario:scenarios) {
        LibraryEntry entry;
        entry.id={"fixture-user",std::string(32,'a')};
        entry.book.book.author="Fixture author";
        entry.availability=scenario.availability;entry.book.epubs=scenario.epubs;
        entry.book.book.deleted=scenario.deleted;entry.upload_pending=scenario.upload_pending;
        entry.book.local_only=scenario.local_only;
        const auto expected=QString::fromUtf8(scenario.expected);
        assert(bookStatusLabel(entry)==expected);
        auto* model=control.library();
        model->replace({entry});
        control.selectBook(QString::fromStdString(entry.id.account),QString::fromStdString(entry.id.hash));
        assert(model->data(model->index(0),LibraryModel::Availability).toString()==expected);
        assert(control.book().value("availability").toString()==expected);
        assert(control.hint().startsWith(expected+". Fixture author\n"));
    }
}
static void bookActionChecks() {
    auto commands=[](const QVariantList& actions) {
        QStringList result;
        for(const auto& action:actions) result.append(action.toMap().value("command").toString());
        return result.join(',');
    };
    auto check=[&](const BookActions& actions,const char* allowed,const char* primary,
                  const char* secondary,const char* menu,const char* choices,bool choosing=false) {
        assert(commands(actions.allowed)==allowed);
        assert(actions.primary.value("command").toString()==primary);
        assert(commands(actions.secondary)==secondary);
        assert(commands(actions.menu)==menu);
        assert(commands(actions.choices)==choices);
        assert(actions.choosing==choosing);
    };
    LibraryEntry entry; entry.availability=Availability::OnDevice; entry.book.epubs=1;
    auto plan=[&](BookChoice choice=BookChoice::None,BookBlock block=BookBlock::None,bool signedIn=true) {
        return bookActions(entry,signedIn,choice,block);
    };
    check(plan(),"open,sync,offline,uploadCover,back","open","sync,offline","uploadCover","");
    entry.sync.pending_remote="epubcfi(/6/2!/4/2)";
    assert(plan().primary.value("text")=="Open at Readest position");
    entry.book.epubs=0;
    check(plan(),"open,sync,offline,upload,back","open","sync,offline,upload","","");
    entry.upload_pending=true;
    check(plan(),"upload,back","upload","","","");
    assert(plan().primary.value("text")=="Retry upload / reading position");
    check(plan(BookChoice::SyncConflict),"usePocketBook,useReadest,back","","","","usePocketBook,useReadest");
    check(plan(BookChoice::OpenConflict),"openPocketBook,openReadest,back","","","","openPocketBook,openReadest");
    check(plan(BookChoice::None,BookBlock::Pending),"","","","","");
    check(plan(BookChoice::None,BookBlock::Acknowledged),"sync,back","sync","","","");
    entry.upload_pending=false; entry.book.local_only=true;
    check(plan(),"offline,upload,back","offline","upload","","");
    assert(plan().secondary.front().toMap().value("text")=="Upload to Readest");
    entry.book.book.deleted=true;
    assert(plan().secondary.front().toMap().value("text")=="Re-upload to Readest");
    check(plan(BookChoice::None,BookBlock::None,false),"offline,signin,back","offline","signin","","");
    entry.book.local_only=false; entry.book.book.deleted=false;
    entry.book.copies.resize(2);
    check(plan(),"copies,open,sync,offline,upload,back","copies","open,sync,offline,upload","","");
    check(plan(BookChoice::LocalCopy),"copy:0,copy:1,back","","copy:0,copy:1","","",true);
    entry.book.needs_copy_choice=true;
    check(plan(),"copy:0,copy:1,back","","copy:0,copy:1","","",true);
    entry.book.needs_copy_choice=false; entry.book.copies.clear();
    entry.availability=Availability::Downloadable;
    check(plan(),"download,back","download","","","");
    entry.availability=Availability::Unknown;
    check(plan(),"refresh,back","refresh","","","");
    check(plan(BookChoice::None,BookBlock::Acknowledged),"refresh,back","refresh","","","");
}
static void runnerChecks() {
    {
        // An online operation must hold standby off before waking Wi-Fi,
        // through the worker, and release it on every completion path.
        bool awake=false,completed=false;int executions=0;
        DeviceAccess device;
        device.keepAwake=[&](bool value) { assert(awake!=value);awake=value; };
        device.ping=[] {};
        device.connectionTimeoutMs=1000;
        device.connect=[&](std::function<void(int)> callback) { assert(awake);callback(0);return true; };
        auto work=[&](const std::atomic<bool>&) { assert(awake);++executions;return OperationResult{}; };
        auto complete=[&](OperationResult) { assert(!awake);completed=true; };
        {
            OperationRunner runner(device);
            assert(runner.start(work,true,complete,[](bool) {}));
            settle(450);assert(completed && !awake && executions==1);
        }
        device.connect=[&](std::function<void(int)>) { assert(awake);return false; };
        {
            OperationRunner runner(device);
            assert(!runner.start(work,true,complete,[](bool) {}));assert(!awake);
        }
        device.connect=[&](std::function<void(int)>) { assert(awake);return true; };
        device.connectionTimeoutMs=20;
        for(bool cancel:{false,true}) {
            completed=false;
            OperationRunner runner(device);
            assert(runner.start(work,true,complete,[](bool) {}));assert(awake);
            if(cancel) runner.cancel();
            settle(250);assert(completed && !awake);
            assert(!runner.start(work,true,complete,[](bool) {}));assert(!awake);
        }
        {
            OperationRunner runner(device);
            assert(runner.start(work,true,complete,[](bool) {}));assert(awake);
        }
        assert(!awake && executions==1);
    }
    {
        platform_test::sleeping_wifi=true;platform_test::wake_calls=0;
        auto device=deviceAccess();device.connectionTimeoutMs=400;
        OperationRunner runner(device);bool connected=false;
        runner.start([](const std::atomic<bool>&) { return OperationResult{}; },true,
            [&](OperationResult r) { connected=r.outcome==Outcome::Ready; },[](bool) {});
        settle(650);
        assert(connected && platform_test::wake_calls==1 && !platform_test::sleeping_wifi);
    }
    {
        // Firmware keepalive may block; the UI deadline must still fire.
        platform_test::hold_ping=true;platform_test::ping_entered=false;platform_test::ping_finished=false;platform_test::ping_calls=0;
        auto device=deviceAccess();device.connectionTimeoutMs=20;
        device.networkReady=[] { return false; };
        device.ping();
        device.connect=[](std::function<void(int)>) { return true; };
        bool timed_out=false,worked=false;
        OperationRunner runner(device);
        runner.start([&](const std::atomic<bool>&) { worked=true;return OperationResult{}; },true,
            [&](OperationResult r) { timed_out=r.outcome==Outcome::Failed; },[](bool) {});
        settle(300);
        device.ping();device.ping(); // A stalled call must not spawn more workers.
        const bool responsive=timed_out && !worked && platform_test::ping_entered && !platform_test::ping_finished;
        platform_test::hold_ping=false;
        settle(50);
        assert(responsive && platform_test::ping_finished && platform_test::ping_calls==1);
    }
    for(const auto& row : {"", "Iface Destination Gateway Flags RefCnt Use Metric Mask",
            "wlan0 0010A8C0 00000000 0001 0 0 0 00FFFFFF",
            "wlan0 00000000 0100A8C0 0000 0 0 0 00000000",
            "wlan0 00000000 0100A8C0 0201 0 0 0 00000000",
            "lo 00000000 00000000 0001 0 0 0 00000000"}) {
        std::istringstream routes(row); assert(!has_default_route(routes));
    }
    std::istringstream routes("Iface Destination Gateway Flags RefCnt Use Metric Mask\n"
        "wlan0 00000000 0100A8C0 0003 0 0 600 00000000\n");
    assert(has_default_route(routes));
    DeviceAccess device;
    std::function<void(int)> pending;
    device.connect=[&](std::function<void(int)> callback) { if(pending) return false; pending=std::move(callback); return true; };
    device.ping=[] {}; device.connectionTimeoutMs=20;
    int completed=0,worked=0;
    auto task=[&](const std::atomic<bool>&) { ++worked; return OperationResult{}; };
    {
        OperationRunner runner(device);
        assert(runner.start(task,true,[&](OperationResult r) { assert(r.outcome==Outcome::Failed); ++completed; },[](bool) {}));
        assert(!runner.start(task,false,[](OperationResult) {},[](bool) {}));
        settle(250); assert(!runner.busy() && completed==1 && worked==0);
        assert(!runner.start(task,true,[](OperationResult) {},[](bool) {}));
        assert(runner.start(task,false,[&](OperationResult) { ++completed; },[](bool) {}));
        settle(250); assert(completed==2 && worked==1);
    }
    // A late firmware callback is safe after destruction and cannot start old work.
    auto late=std::move(pending); pending=nullptr; late(0); assert(worked==1);
    {
        OperationRunner runner(device);
        assert(runner.start(task,true,[&](OperationResult r) { assert(r.outcome==Outcome::Cancelled); ++completed; },[](bool) {}));
        runner.cancel(); settle(250); assert(worked==1 && completed==3);
    }
    late=std::move(pending); pending=nullptr; late(0);
    {
        device.connectionTimeoutMs=2000;
        OperationRunner runner(device);
        assert(runner.start(task,true,[](OperationResult r) { assert(r.outcome==Outcome::Cancelled); },[](bool) {}));
        runner.cancel(); settle(250);
        // A foreground sync takes over a cancelled cover connection, without
        // losing its work or issuing another firmware connection request.
        bool synced=false;
        assert(runner.start(task,true,[&](OperationResult r) { assert(r.outcome==Outcome::Ready); synced=true; },[](bool) {}));
        auto connected=std::move(pending); pending=nullptr; connected(0);
        settle(500); assert(synced && worked==2);
    }
    std::atomic<bool> stopped{false};
    {
        OperationRunner runner(device);
        runner.start([&](const std::atomic<bool>& cancel) {
            while(!cancel.load()) QThread::msleep(1);
            stopped=true; return OperationResult{};
        },false,[](OperationResult) { assert(false); },[](bool) {});
    }
    assert(stopped);
    {
        auto waiting=device;
        waiting.connect=[](std::function<void(int)> callback) { callback(0);return true; };
        waiting.networkReady=[] { return false; };waiting.connectionTimeoutMs=20;
        OperationRunner runner(waiting);bool timed_out=false;
        runner.start([](const std::atomic<bool>&) { assert(false);return OperationResult{}; },true,
            [&](OperationResult r) { timed_out=r.outcome==Outcome::Failed; },[](bool) {});
        settle(250);assert(timed_out && !runner.busy());
    }
    {
        OperationRunner runner(device);
        runner.start([](const std::atomic<bool>&)->OperationResult { throw std::runtime_error("fixture error"); },false,
            [&](OperationResult r) { assert(r.outcome==Outcome::Failed && r.error=="fixture error"); ++completed; },[](bool) {});
        settle(250); assert(completed==4);
    }
    {
        bool ready=true; int connections=0,executions=0;
        device.networkReady=[&] { return ready; };
        device.connect=[&](std::function<void(int)> callback) {
            ++connections; assert(!pending); pending=std::move(callback); return true;
        };
        OperationRunner runner(device);
        auto work=[&](const std::atomic<bool>&) { ++executions; return OperationResult{}; };
        auto finish=[](OperationResult r) { assert(r.outcome==Outcome::Ready); };
        assert(runner.start(work,true,finish,[](bool connecting) { assert(!connecting); }));
        settle(250); assert(executions==1 && connections==0);
        ready=false;
        assert(runner.start(work,true,finish,[](bool) {}));
        settle(250); assert(runner.busy() && executions==1 && connections==1);
        // The route appears, but the firmware never delivers its callback.
        ready=true; settle(500); assert(!runner.busy() && executions==2 && pending);
        assert(runner.start(work,true,finish,[](bool) {}));
        settle(250); assert(executions==3 && connections==1);
        auto late_reply=std::move(pending); pending=nullptr; late_reply(-1);
        assert(executions==3); // A late error must not undo completed work.
        ready=false;
        assert(runner.start(work,true,finish,[](bool) {}));
        assert(connections==2);
        auto reply=std::move(pending); pending=nullptr; reply(0);
        settle(250);assert(runner.busy() && executions==3);
        ready=true;settle(250); assert(executions==4);
    }
}
int main(int argc,char** argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv); bookStatusChecks(); bookActionChecks(); runnerChecks();
    QTemporaryDir temp; assert(temp.isValid());
    auto config=configAt(temp.path());
    std::atomic<int> cover_requests{0};
    std::atomic<bool> hold_foreground{false};
    bool cover_scenario=false;
    const auto transport=config.transport.request;
    config.transport.request=[&](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
        const std::string& body,const std::string& ca,size_t cap,const std::atomic<bool>& cancel) {
        if(hold_foreground && url.find("/api/sync?")!=std::string::npos) {
            while(hold_foreground && !cancel.load()) QThread::msleep(1);
            if(cancel.load()) throw std::runtime_error("Cancelled.");
        }
        if(cover_scenario) {
            HttpResponse r; r.status=200;
            if(url.find("/api/sync?")!=std::string::npos) { r.body=R"({"books":[]})"; return r; }
            if(url.find("/api/storage/list?")!=std::string::npos) { r.body=R"({"page":1,"totalPages":1,"files":[]})"; return r; }
            if(url.find("/api/storage/download?")!=std::string::npos) { ++cover_requests; QThread::msleep(300); r.status=404; return r; }
        }
        return transport(url,method,headers,body,ca,cap,cancel);
    };
    std::string first_hash;
    {
        State state(config.root+"/state.db"); LibraryPage page; page.cursor=1;
        std::map<std::string,BookFiles> files;
        for(int i=0;i<51;++i) {
            LibraryBook b; b.hash=std::string(30,'0')+(i<10?"0":"")+std::to_string(i);
            if(i==0) first_hash=b.hash;
            b.title="Book "+std::to_string(i); b.author="Example Author"; b.format="EPUB";
            b.raw=i%2?R"({"progress":[15,60],"updated_at":100})":"{}";
            page.books.push_back(b); files[b.hash].epubs=i==0?1:i%2;
            if(i%7==0) {
                QImage cover(904,1317,QImage::Format_Grayscale8); cover.fill(100+i);
                const auto path=QString::fromStdString(cover_path(config.root,"fixture-user",b.hash,files[b.hash]));
                assert(cover.save(path,i==7?"JPEG":"PNG"));
                CoverProvider provider(QString::fromStdString(config.root)); QSize size;
                const auto decoded=provider.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(path)),&size,QSize(324,485));
                assert(!decoded.isNull() && decoded.width()==324 && decoded.height()==472);
                if(i==0) {
                    const auto payload=QString::fromStdString("fixture-user\n"+b.hash+"\n")+path;
                    const auto request="thumb/"+QString::fromUtf8(QUrl::toPercentEncoding(payload));
                    const auto thumbnail=provider.requestImage(request,&size,QSize(105,155));
                    assert(!thumbnail.isNull() && thumbnail.width()<=105 && thumbnail.height()<=155);
                    QFile source(path); assert(source.open(QIODevice::ReadOnly));
                    const auto original=source.readAll(); source.close();
                    const auto stamp=QFileInfo(path).lastModified();
                    const auto setStamp=[&](const QDateTime& value) {
                        assert(source.open(QIODevice::ReadWrite));
                        const bool changed=source.setFileTime(value,QFileDevice::FileModificationTime);
                        source.close(); assert(changed);
                    };
                    auto damaged=original;
                    for(int byte=32;byte<damaged.size();++byte) damaged[byte]=0;
                    assert(source.open(QIODevice::WriteOnly|QIODevice::Truncate)); assert(source.write(damaged)==damaged.size()); source.close();
                    setStamp(stamp);
                    CoverProvider restarted(QString::fromStdString(config.root));
                    assert(restarted.requestImage(request,&size,QSize(105,155))==thumbnail); // Warm restart avoids source decoding.
                    setStamp(stamp.addSecs(2));
                    CoverProvider invalidated(QString::fromStdString(config.root));
                    assert(invalidated.requestImage(request,&size,QSize(105,155)).isNull()); // A changed source cannot reuse stale pixels.
                    assert(source.open(QIODevice::WriteOnly|QIODevice::Truncate)); assert(source.write(original)==original.size()); source.close();
                    setStamp(stamp.addSecs(4));
                    assert(!invalidated.requestImage(request,&size,QSize(105,155)).isNull());
                }
            }
        }
        const auto failedRoot=temp.path()+"/failed-thumbnail-cache";
        assert(QDir().mkpath(failedRoot+"/cover-cache.db"));
        const auto failedPath=failedRoot+"/cover-device-"+QString(32,'a')+"-1-1.png";
        QImage fallback(20,30,QImage::Format_Grayscale8); fallback.fill(80); assert(fallback.save(failedPath,"PNG"));
        const auto failedPayload="device\n"+QString(32,'a')+"\n"+failedPath;
        CoverProvider failed(failedRoot); QSize failedSize;
        assert(!failed.requestImage("thumb/"+QString::fromUtf8(QUrl::toPercentEncoding(failedPayload)),&failedSize,QSize(10,15)).isNull());
        state.apply_page("fixture-user",0,page); state.save_book_files("fixture-user",files);
        StoredBook stored; stored.path=(temp.path()+"/local.epub").toStdString();
        { QFile file(QString::fromStdString(stored.path)); assert(file.open(QIODevice::WriteOnly)); file.write("x"); }
        stored.integrity.readest_hash=first_hash; stored.integrity.sha256=std::string(64,'a'); stored.integrity.size=1;
        state.register_download("fixture-user",first_hash,stored);
        SavedSync saved; saved.pending_remote="epubcfi(/6/4!/4/2)";
        saved.remote_config=R"({"progress":[30,60],"updatedAt":200})"; state.save_sync("fixture-user",first_hash,saved);
        const std::atomic<bool> cancel{false};
        Cloud cloud(config.root+"/session.json",config.ca,config.public_key,config.auth_origin,config.api_origin,config.transport.bind_request(cancel));
        cloud.sign_in("test","test",1);
    }
    DeviceAccess device;
    device.connect=[](std::function<void(int)> callback) { callback(0); return true; };
    device.ping=[] {}; device.open=[](const QString&) { return true; };
    assert(QDir().mkpath(QString::fromStdString(config.database).section('/',0,-2)));
    sqlite3* native=nullptr; assert(sqlite3_open(config.database.c_str(),&native)==SQLITE_OK);
    assert(sqlite3_exec(native,"CREATE TABLE files(book_id,folder_id,storageid,filename,fast_hash);CREATE TABLE folders(id,storageid,name);CREATE TABLE books_settings(bookid,profileid,position,position_ts,cpage,npage);",nullptr,nullptr,nullptr)==SQLITE_OK);
    assert(sqlite3_close(native)==SQLITE_OK);
    AppController control(config,device); control.initialize(); finish(control);
    assert(control.initialized() && control.signedIn());
    auto* model=control.library(); assert(model->count()==51 && model->pages()==9 && model->rowCount()==6);
    const BookId first{"fixture-user",first_hash};
    assert(model->find(first)->remote_percentage==50);
    control.selectBook("other-user",QString::fromStdString(first_hash)); assert(!control.detail());
    control.selectBook("fixture-user",QString::fromStdString(first_hash));
    assert(control.detail() && control.actions().front().toMap()["text"]=="Open at Readest position");
    assert(control.status().isEmpty());
    assert(control.book()["title"]=="Book 0" && control.book()["author"]=="Example Author");
    assert(control.book()["pocketBookProgress"]=="—" && control.book()["readestProgress"]=="50.0%");
    control.runAction("download"); assert(!control.busy()); // Unavailable commands cannot bypass the screen contract.
    control.back();
    for(int i=0;i<20;++i) control.turnPage(1);
    assert(model->page()==9 && model->rowCount()==3);
    control.search("Book 50"); assert(model->page()==1 && model->count()==1);
    const auto selected=model->at(0).id;
    control.selectBook(QString::fromStdString(selected.account),QString::fromStdString(selected.hash));
    assert(control.actions().front().toMap()["command"]=="refresh"); control.back();
    control.search(""); control.setAvailabilityFilter(1); assert(model->count()==25);
    control.search("Book 1"); assert(model->count()==6);
    control.setAvailabilityFilter(2); assert(model->count()==0);
    control.search(""); assert(model->count()==1);
    control.setAvailabilityFilter(3); assert(model->count()==25);
    control.setAvailabilityFilter(0); assert(model->count()==51);
    control.setPageCapacity(4); assert(model->rowCount()==4 && model->pages()==13);
    control.turnPage(1); const auto rotation_anchor=model->at(0).id;
    control.setPageCapacity(6);
    bool anchor_visible=false;
    for(int row=0;row<model->rowCount();++row) if(model->at(row).id==rotation_anchor) anchor_visible=true;
    assert(anchor_visible);
    // The model finds the same identity after reorder; presentation data does no I/O.
    LibraryModel independent;
    auto reversed=model->entries(); std::reverse(reversed.begin(),reversed.end()); independent.replace(reversed);
    assert(independent.find(first)->id==first); independent.search("example author"); assert(independent.count()==51);
    LibraryEntry local; local.id={"",std::string(32,'b')};local.book.local_only=true;local.availability=Availability::OnDevice;
    independent.replace({local,*model->find(first)}); independent.search("");
    independent.filter(4);assert(independent.count()==1 && independent.at(0).book.local_only);
    independent.filter(2);assert(independent.count()==2);
    // Separate instances and headless application operations have independent state.
    const auto other=configAt(temp.path()+"/other");
    { QFile file(QString::fromStdString(other.root+"/session.json")); assert(file.open(QIODevice::WriteOnly)); file.write("{bad"); }
    ApplicationService service(other); std::atomic<bool> cancel{false};
    const auto recovered=service.execute(Request{},cancel);
    assert(recovered.library.initialized && !recovered.library.signed_in && recovered.outcome==Outcome::SessionInvalid);
    AppController second(other,device); second.initialize(); finish(second);
    assert(second.initialized() && !second.signedIn() && control.signedIn());
    second.signIn("",""); assert(!second.busy() && second.status()=="Enter both your email and password first.");
    Request stale; stale.command=Command::Download; stale.book=first;
    assert(service.execute(stale,cancel).outcome==Outcome::Failed);
    {
        State state(other.root+"/state.db");
        LocalCopy a; a.path=(temp.path()+"/local.epub").toStdString();a.hash=std::string(32,'c');a.title="Device only";a.size=1;a.position="alpha";
        LocalCopy b=a;b.path=(temp.path()+"/copy.epub").toStdString();b.position="bravo";
        assert(QFile::copy(QString::fromStdString(a.path),QString::fromStdString(b.path)));
        state.replace_local_copies({a,b});
    }
    AppController localControl(other,device);localControl.initialize();finish(localControl);
    localControl.selectBook("",QString(32,'c'));assert(localControl.detail());
    assert(localControl.actions().front().toMap()["command"]=="copy:0");
    localControl.runAction("copy:1");finish(localControl);
    assert(localControl.actions().front().toMap()["command"]=="offline");
    localControl.runAction("signin");assert(localControl.signingIn());localControl.back();assert(!localControl.signingIn());
    {
        // Cloud deletion stays visible for local files, with an explicit restore
        // action. A matching progress-only cloud row also offers EPUB upload.
        const auto recovery=configAt(temp.path()+"/recovery");
        const std::atomic<bool> not_cancelled{false};
        Cloud auth(recovery.root+"/session.json",recovery.ca,recovery.public_key,recovery.auth_origin,recovery.api_origin,
            recovery.transport.bind_request(not_cancelled));
        auth.sign_in("test","test",1);
        const auto hash=std::string(32,'d');
        {
            State state(recovery.root+"/state.db");LibraryPage page;page.cursor=1;
            LibraryBook book;book.hash=hash;book.title="Removed book";book.format="EPUB";book.deleted=true;
            page.books.push_back(book);state.apply_page("fixture-user",0,page);
            StoredBook file;file.path=(temp.path()+"/local.epub").toStdString();file.integrity.readest_hash=hash;
            file.integrity.sha256=std::string(64,'a');file.integrity.size=1;state.register_download("fixture-user",hash,file);
            state.save_book_files("fixture-user",{});
        }
        auto has_action=[](AppController& controller,const QString& name) {
            for(const auto& action:controller.actions()) if(action.toMap()["command"]==name) return true;
            return false;
        };
        AppController removed(recovery,device);removed.initialize();finish(removed);
        removed.selectBook("fixture-user",QString::fromStdString(hash));
        assert(removed.hint().startsWith("Removed from Readest"));
        assert(has_action(removed,"upload") && has_action(removed,"offline") && !has_action(removed,"sync"));
        {
            State state(recovery.root+"/state.db");LibraryPage page;page.cursor=2;
            LibraryBook book;book.hash=hash;book.title="Progress only book";book.format="EPUB";
            page.books.push_back(book);state.apply_page("fixture-user",1,page);
        }
        AppController progressOnly(recovery,device);progressOnly.initialize();finish(progressOnly);
        progressOnly.selectBook("fixture-user",QString::fromStdString(hash));
        assert(progressOnly.hint().contains("Readest has progress only"));
        assert(has_action(progressOnly,"upload") && has_action(progressOnly,"sync"));
        assert(QFile::exists(temp.path()+"/local.epub"));
    }
    QQmlApplicationEngine engine; engine.addImportPath(READEST_TEST_CONTROLS);
    engine.addImageProvider("cover",new CoverProvider(QString::fromStdString(config.root)));
    engine.rootContext()->setContextProperty("appController",&control);
    engine.rootContext()->setContextProperty("screenWidth",1404); engine.rootContext()->setContextProperty("screenHeight",1800);
    bool warnings=false;
    QObject::connect(&engine,&QQmlEngine::warnings,[&](const QList<QQmlError>& errors) {
        warnings=true; for(const auto& error:errors) std::cerr<<error.toString().toStdString()<<"\n";
    });
    engine.load(QUrl("qrc:/Main.qml")); assert(!engine.rootObjects().isEmpty());
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); assert(window);
    assert(window->findChild<QObject*>("nativeOperationSurface"));
    assert(window->findChild<QObject*>("nativeDecisionDialog"));
    assert(window->findChild<QObject*>("nativeMessageBand"));
    for(bool wide:{false,true}) {
        window->resize(wide?1800:1404,wide?1404:1800); settle();
        for(int i=0;i<13;++i) { control.turnPage(1); settle(10); }
        for(int i=0;i<13;++i) { control.turnPage(-1); settle(10); }
        assert(control.library()->rowCount()==(wide?6:8));
        assert(count_named(window->contentItem(),"detailedBookRow")==control.library()->rowCount());
        auto picture=window->grabWindow(); assert(!picture.isNull());
        if(const auto output=qEnvironmentVariable("READEST_UI_PREVIEW"); !output.isEmpty())
            assert(picture.save(output+(wide?"-landscape.png":"-portrait.png")));
    }
    auto* libraryPage=find_item(window->contentItem(),"nativeLibraryPage"); assert(libraryPage);
    for(int attempt=0;attempt<3;++attempt) {
        tap_book(window);
        assert(control.detail() && find_item(window->contentItem(),"nativeBookDetailsPage"));
        control.back(); settle();
    }
    libraryPage=find_item(window->contentItem(),"nativeLibraryPage"); assert(libraryPage);
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu))); settle();
    assert(libraryPage->property("menuOpen").toBool());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu))); settle();
    assert(!libraryPage->property("menuOpen").toBool());
    hold_foreground=true; control.refreshLibrary(); settle(); assert(control.busy());
    tap_book(window,QGuiApplication::styleHints()->mousePressAndHoldInterval()+100);
    assert(!libraryPage->property("contextOpen").toBool());
    tap_book(window); assert(!control.detail());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu))); settle();
    assert(!libraryPage->property("menuOpen").toBool());
    hold_foreground=false; finish(control);
    tap_book(window);
    assert(control.detail() && find_item(window->contentItem(),"nativeBookDetailsPage"));
    control.back(); settle();
    libraryPage=find_item(window->contentItem(),"nativeLibraryPage"); assert(libraryPage);
    auto* header=window->findChild<QObject*>("nativeHeader"); assert(header);
    // The same shell receives Menu from both the header and hardware.
    QVariantMap menuView{{"initialized",true},{"signingIn",false},{"detail",false},
        {"busy",false},{"decision",false},{"blocking",false},{"signedIn",true},
        {"title","Readest Sync"},{"status",""},{"statusKind","none"},{"blockingMessage",""},
        {"actions",QVariantList{}},{"library",QVariant::fromValue(control.library())}};
    for(const auto& guard:{QString(),QString("busy"),QString("decision"),QString("blocking")}) {
        auto view=menuView;
        if(!guard.isEmpty()) view[guard]=true;
        window->setProperty("view",view); settle();
        if(!guard.isEmpty()) {
            tap_book(window,QGuiApplication::styleHints()->mousePressAndHoldInterval()+100);
            assert(!libraryPage->property("contextOpen").toBool());
            tap_book(window);
            assert(!libraryPage->property("activating").toBool());
        }
        for(bool hardware:{false,true}) {
            libraryPage->setProperty("menuOpen",false);
            if(hardware) QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu)));
            else QMetaObject::invokeMethod(header,"action");
            assert(libraryPage->property("menuOpen").toBool()==guard.isEmpty());
        }
    }
    window->setProperty("view",QVariant::fromValue(&control)); settle();
    // Dialog controls must still receive real taps above the shared shield.
    for(const auto& mode:{QString("busy"),QString("decision"),QString("blocking")}) {
        auto view=engine.toScriptValue(menuView);
        view.setProperty(mode,true);
        view.setProperty("lastCommand",QString());
        view.setProperty("close",engine.evaluate("(function() { this.lastCommand = 'close'; })"));
        view.setProperty("cancelDecision",engine.evaluate("(function() { this.lastCommand = 'cancel'; })"));
        view.setProperty("acknowledgeBlocking",engine.evaluate("(function() { this.lastCommand = 'acknowledge'; })"));
        view.setProperty("runAction",engine.evaluate("(function(command) { this.lastCommand = command; })"));
        view.setProperty("actionPresentation",engine.toScriptValue(QVariantMap{{"choices",QVariantList{
            QVariantMap{{"command","usePocketBook"},{"text","Use PocketBook position"}},
            QVariantMap{{"command","useReadest"},{"text","Use Readest position"}}}}}));
        window->setProperty("view",QVariant::fromValue(view)); settle();
        if(mode=="decision") {
            for(const auto& command:{QString("usePocketBook"),QString("useReadest")}) {
                tap_item(window,find_item(window->contentItem(),"nativeDecisionChoice_"+command));
                assert(view.property("lastCommand").toString()==command);
            }
        }
        tap_item(window,find_item(window->contentItem(),mode=="busy"?"nativeOperationCancel":"nativeDecisionDismiss"));
        assert(view.property("lastCommand").toString()==(mode=="busy"?"close":mode=="decision"?"cancel":"acknowledge"));
    }
    window->setProperty("view",QVariant::fromValue(&control)); settle();
    tap_book(window,QGuiApplication::styleHints()->mousePressAndHoldInterval()+100);
    assert(libraryPage->property("contextOpen").toBool());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Back))); settle();
    assert(!libraryPage->property("contextOpen").toBool());
    libraryPage->setProperty("menuOpen",true); settle();
    assert(window->findChild<QQuickItem*>("nativeLibraryMenu")->isVisible());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Back))); settle();
    assert(!libraryPage->property("menuOpen").toBool());
    QMetaObject::invokeMethod(libraryPage,"runMenuAction",Q_ARG(QVariant,QVariant(0))); settle();
    assert(libraryPage->property("searchOpen").toBool() && window->findChild<QObject*>("searchInput"));
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Back))); settle();
    assert(!libraryPage->property("searchOpen").toBool());
    QMetaObject::invokeMethod(libraryPage,"runMenuAction",Q_ARG(QVariant,QVariant(1))); settle();
    assert(libraryPage->property("filterMenuOpen").toBool() && window->findChild<QQuickItem*>("nativeFilterMenu")->isVisible());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Back))); settle();
    assert(!libraryPage->property("filterMenuOpen").toBool());
    window->resize(1404,1800); settle();
    control.selectBook("fixture-user",QString::fromStdString(first_hash)); settle();
    assert(control.actions().size()==5);
    auto* detailsPage=find_item(window->contentItem(),"nativeBookDetailsPage"); assert(detailsPage);
    assert(find_item(window->contentItem(),"nativePrimaryAction")->isVisible());
    assert(detailsPage->property("primaryAction").toMap()==control.actionPresentation().value("primary").toMap());
    assert(detailsPage->property("menuActions").toList()==control.actionPresentation().value("menu").toList());
    assert(detailsPage->property("hasMenu").toBool());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu))); settle();
    assert(detailsPage->property("menuOpen").toBool());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Menu))); settle();
    assert(!detailsPage->property("menuOpen").toBool());
    if(const auto output=qEnvironmentVariable("READEST_UI_PREVIEW"); !output.isEmpty())
        assert(window->grabWindow().save(output+"-detail-portrait.png"));
    QMetaObject::invokeMethod(detailsPage,"toggleMenu"); settle();
    assert(find_item(window->contentItem(),"nativeBookMenu")->isVisible());
    QMetaObject::invokeMethod(window,"handleHardwareButton",Q_ARG(QVariant,QVariant(Qt::Key_Back))); settle();
    assert(control.detail() && !detailsPage->property("menuOpen").toBool());
    window->resize(1800,1404); settle(); assert(control.detail() && window->findChild<QObject*>("nativeBookDetailsPage"));
    if(const auto output=qEnvironmentVariable("READEST_UI_PREVIEW"); !output.isEmpty())
        assert(window->grabWindow().save(output+"-detail-landscape.png"));
    window->resize(1404,1800); settle(); control.back();
    cover_scenario=true;
    control.refreshLibrary(); finish(control);
    assert(control.status().startsWith("Library refreshed"));
    settle(450);
    assert(!control.busy() && cover_requests>0); // Background cover work leaves library interaction available.
    control.scanDevice(); assert(control.busy()); finish(control);
    assert(control.status().startsWith("Device scan complete") && cover_requests<=2); // Foreground work interrupts the cover batch.
    control.signOut(); finish(control); settle();
    control.showSignIn();settle();
    assert(window->findChild<QObject*>("emailInput") && window->findChild<QObject*>("passwordInput"));
    control.signIn("",""); settle();
    bool warningVisible=false;
    for(auto* band:window->findChildren<QQuickItem*>("nativeMessageBand")) warningVisible|=band->isVisible();
    assert(control.statusKind()=="warning" && warningVisible);
    if(const auto output=qEnvironmentVariable("READEST_UI_PREVIEW"); !output.isEmpty()) {
        window->resize(1404,1800); settle(); assert(window->grabWindow().save(output+"-sign-in-warning-portrait.png"));
        window->resize(1800,1404); settle(); assert(window->grabWindow().save(output+"-sign-in-warning-landscape.png"));
    }
    assert(!warnings);
    std::cout<<"Application isolation, public commands, model/filter identity, runner lifetime and QML checks passed.\n";
}
