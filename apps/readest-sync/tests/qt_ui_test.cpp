// Link the production controller/model/runner normally; test public contracts.
#include "controller.h"
#include "cover_provider.h"
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QQuickItem>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QFile>
#include <cassert>
#include <iostream>
using namespace readest;
static void settle(int milliseconds=100) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static void finish(AppController& control) {
    QElapsedTimer timer; timer.start();
    while(control.busy() && timer.elapsed()<5000) settle(10);
    assert(!control.busy());
}
static int check_progress_layout(QQuickItem* item) {
    int count=0;
    if(item->objectName()=="bookProgress") { assert(item->y()+item->height()<=item->parentItem()->height()+1); ++count; }
    for(auto* child:item->childItems()) count+=check_progress_layout(child);
    return count;
}
static ApplicationConfig configAt(const QString& base) {
    ApplicationConfig config;
    config.root=(base+"/system/readest-sync").toStdString();
    config.books_root=(base+"/Books/Readest").toStdString();
    config.database=(base+"/system/explorer-3/explorer-3.db").toStdString();
    config.ca="test-ca"; config.public_key="public"; config.book_roots={base.toStdString()};
    config.auth_origin="https://auth.test"; config.api_origin="https://api.test";
    config.transport=[](const std::string& url,const std::string&,const std::vector<std::string>&,const std::string&,const std::string&,size_t) {
        HttpResponse r; r.status=500; r.body="{}";
        if(url.find("grant_type=password")!=std::string::npos) {
            r.status=200;
            r.body=R"({"access_token":"test-access","refresh_token":"test-refresh","expires_at":9999999999,"user":{"id":"fixture-user"}})";
        }
        return r;
    };
    assert(QDir().mkpath(QString::fromStdString(config.root)));
    return config;
}
static void runnerChecks() {
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
        OperationRunner runner(device);
        runner.start([](const std::atomic<bool>&)->OperationResult { throw std::runtime_error("fixture error"); },false,
            [&](OperationResult r) { assert(r.outcome==Outcome::Failed && r.error=="fixture error"); ++completed; },[](bool) {});
        settle(250); assert(completed==4);
    }
}
int main(int argc,char** argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv); runnerChecks();
    QTemporaryDir temp; assert(temp.isValid());
    auto config=configAt(temp.path());
    std::atomic<int> cover_requests{0}; bool cover_scenario=false;
    const auto transport=config.transport;
    config.transport=[&](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
        const std::string& body,const std::string& ca,size_t cap) {
        if(cover_scenario) {
            HttpResponse r; r.status=200;
            if(url.find("/api/sync?")!=std::string::npos) { r.body=R"({"books":[]})"; return r; }
            if(url.find("/api/storage/list?")!=std::string::npos) { r.body=R"({"page":1,"totalPages":1,"files":[]})"; return r; }
            if(url.find("/api/storage/download?")!=std::string::npos) { ++cover_requests; QThread::msleep(300); r.status=404; return r; }
        }
        return transport(url,method,headers,body,ca,cap);
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
            page.books.push_back(b); files[b.hash].epubs=i%2;
            if(i%7==0) {
                QImage cover(904,1317,QImage::Format_Grayscale8); cover.fill(100+i);
                const auto path=QString::fromStdString(cover_path(config.root,"fixture-user",b.hash,files[b.hash]));
                assert(cover.save(path,i==7?"JPEG":"PNG"));
                CoverProvider provider(QString::fromStdString(config.root)); QSize size;
                const auto decoded=provider.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(path)),&size,QSize(324,485));
                assert(!decoded.isNull() && decoded.width()==324 && decoded.height()==472);
            }
        }
        state.apply_page("fixture-user",0,page); state.save_book_files("fixture-user",files);
        StoredBook stored; stored.path=(temp.path()+"/local.epub").toStdString();
        { QFile file(QString::fromStdString(stored.path)); assert(file.open(QIODevice::WriteOnly)); file.write("x"); }
        stored.integrity.readest_hash=first_hash; stored.integrity.sha256=std::string(64,'a'); stored.integrity.size=1;
        state.register_download("fixture-user",first_hash,stored);
        SavedSync saved; saved.pending_remote="epubcfi(/6/4!/4/2)";
        saved.remote_config=R"({"progress":[30,60],"updatedAt":200})"; state.save_sync("fixture-user",first_hash,saved);
        Cloud cloud(config.root+"/session.json",config.ca,config.public_key,config.auth_origin,config.api_origin,config.transport);
        cloud.sign_in("test","test",1);
    }
    DeviceAccess device;
    device.connect=[](std::function<void(int)> callback) { callback(0); return true; };
    device.ping=[] {}; device.open=[](const QString&) { return true; };
    AppController control(config,device); control.initialize(); finish(control);
    assert(control.initialized() && control.signedIn());
    auto* model=control.library(); assert(model->count()==51 && model->pages()==9 && model->rowCount()==6);
    const BookId first{"fixture-user",first_hash};
    assert(model->find(first)->remote_percentage==50);
    control.selectBook("other-user",QString::fromStdString(first_hash)); assert(!control.detail());
    control.selectBook("fixture-user",QString::fromStdString(first_hash));
    assert(control.detail() && control.actions().front().toMap()["text"]=="Open at Readest position");
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
    control.setPageCapacity(6);
    // The model finds the same identity after reorder; presentation data does no I/O.
    LibraryModel independent;
    auto reversed=model->entries(); std::reverse(reversed.begin(),reversed.end()); independent.replace(reversed);
    assert(independent.find(first)->id==first); independent.search("example author"); assert(independent.count()==51);
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
    for(bool wide:{false,true}) {
        window->resize(wide?1800:1404,wide?1404:1800); settle();
        for(int i=0;i<13;++i) { control.turnPage(1); settle(10); }
        for(int i=0;i<13;++i) { control.turnPage(-1); settle(10); }
        assert(check_progress_layout(window->contentItem())==(wide?4:6));
        auto picture=window->grabWindow(); assert(!picture.isNull());
        if(const auto output=qEnvironmentVariable("READEST_UI_PREVIEW"); !output.isEmpty())
            assert(picture.save(output+(wide?"-landscape.png":"-portrait.png")));
    }
    control.selectBook("fixture-user",QString::fromStdString(first_hash)); settle();
    assert(control.actions().size()==4); control.back();
    cover_scenario=true;
    control.refreshLibrary(); finish(control);
    assert(control.status().startsWith("Library refreshed"));
    settle(450);
    assert(!control.busy() && cover_requests>0); // Background cover work leaves library interaction available.
    control.scanDevice(); assert(control.busy()); finish(control);
    assert(control.status().startsWith("Device scan complete") && cover_requests<=2); // Foreground work interrupts the cover batch.
    control.signOut(); finish(control); settle();
    assert(window->findChild<QObject*>("emailInput") && window->findChild<QObject*>("passwordInput"));
    assert(!warnings);
    std::cout<<"Application isolation, public commands, model/filter identity, runner lifetime and QML checks passed.\n";
}
