#include <QTemporaryDir>
#include <QByteArray>
#include "controller.h"
#include "cover_provider.h"
#include "platform.h"
#include "simulator.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <fcntl.h>
#include <unistd.h>
using namespace readest;
#include <QElapsedTimer>
#include <QQuickItem>
#include <QThread>
#include <cassert>
#include <iostream>

static void settle(int milliseconds=100) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static void finish(AppController& control) {
    QElapsedTimer timer; timer.start();
    while(control.busy() && timer.elapsed()<15000) settle(10);
    if(control.busy()) std::cerr<<"Timed out: "<<control.status().toStdString()<<"\n";
    assert(!control.busy());
}
static void action(AppController& control,const std::string& label) {
    if(label=="Refresh library") { control.refreshLibrary(); finish(control); return; }
    for(const auto& item:control.actions()) {
        const auto action=item.toMap();
        if(action["text"].toString().toStdString()==label) { control.runAction(action["command"].toString()); finish(control); return; }
    }
    std::cerr<<"Missing action: "<<label<<"; status: "<<control.status().toStdString()<<"\n";
    assert(false);
}

int main(int argc,char** argv) {
    QTemporaryDir scratch("/tmp/readest-simulator-test-XXXXXX");
    assert(scratch.isValid()); qputenv("POCKETBOOK_SIM_ROOT",scratch.path().toUtf8());
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
    Simulator sim; sim.prepare();
    auto config=deviceApplicationConfig(); config.transport=sim.transport();
    std::atomic<bool> cancelled{false};
    auto device=deviceAccess(); device.connectionTimeoutMs=500;
    AppController control(config,device);
    const auto root=config.root,books_root=config.books_root,database=config.database;
    Cloud observer(root+"/observer-session.json",config.ca,config.public_key,config.auth_origin,config.api_origin,config.transport.bind_request(cancelled));
    BookId chosen;
    auto pick=[&](int row) { chosen=control.library()->at(row).id; control.selectBook(QString::fromStdString(chosen.account),QString::fromStdString(chosen.hash)); };
    auto current=[&]() -> const LibraryEntry& { const auto* entry=control.library()->find(chosen); assert(entry); return *entry; };
    auto count=[&] { return control.library()->count(); };
    QQmlApplicationEngine engine;
    engine.addImportPath(READEST_TEST_CONTROLS);
    engine.addImageProvider("cover",new CoverProvider(QString::fromStdString(root)));
    engine.rootContext()->setContextProperty("appController",&control);
    engine.rootContext()->setContextProperty("screenWidth",1404);
    engine.rootContext()->setContextProperty("screenHeight",1800);
    bool warnings=false;
    QObject::connect(&engine,&QQmlEngine::warnings,[&](const QList<QQmlError>& errors) {
        warnings=true; for(const auto& error:errors) std::cerr<<error.toString().toStdString()<<"\n";
    });
    engine.load(QUrl("qrc:/Main.qml")); assert(!engine.rootObjects().isEmpty());
    sim.attach(engine,control);
    if(sim.realCloud()) {
        // A mock session at the old location must not sign in the real profile.
        assert(QDir().mkpath(scratch.path()+"/system/readest-sync"));
        QFile mock(scratch.path()+"/system/readest-sync/session.json");
        assert(mock.open(QIODevice::WriteOnly)); mock.write("mock-session-sentinel"); mock.close();
        control.initialize(); finish(control); assert(control.initialized() && !control.signedIn());
        assert(root==scratch.path().toStdString()+"/real-cloud/system/readest-sync");
        const auto url=qEnvironmentVariable("SIM_TEST_URL").toStdString();
        const auto certificate=qEnvironmentVariable("SIM_TEST_CA").toStdString();
        assert(!url.empty() && !certificate.empty());
        Cloud local(root+"/local-test-session.json",certificate,"test-key",url,url,config.transport.bind_request(cancelled));
        sim.transfer(2); // Mock fault controls cannot replace real requests.
        local.sign_in("demo+ä@example.test","Test@+ü!#",time(nullptr));
        assert(local.session().user_id=="local-test-user");
        assert(fetch_library_page(local,0,100,time(nullptr)).books.empty());
        const auto downloaded=root+"/transport-download";
        const int fd=::open(downloaded.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd>=0);
        auto response=config.transport.download(url+"/download",fd,certificate,100,cancelled); ::close(fd);
        assert(response.status==200 && response.bytes==18);
        bool rejected=false;
        try { config.transport.request(url,"GET",{},"",root+"/missing-ca",100,cancelled); }
        catch(const std::exception&) { rejected=true; }
        assert(rejected);
        const auto book=QString::fromStdString(books_root)+"/real-download.epub";
        assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/00.epub",book));
        assert(sim.open(book)); sim.turnReader(1); sim.remoteChapter(3); sim.closeReader();
        assert(!native_position(database,book.toStdString()).indexed);
        assert(!QFile::exists(scratch.path()+"/real-cloud/remote.json"));
        assert(mock.open(QIODevice::ReadOnly)); assert(mock.readAll()=="mock-session-sentinel"); mock.close();
        settle(); assert(!warnings);
        if(const auto preview=qEnvironmentVariable("READEST_UI_PREVIEW"); !preview.isEmpty()) {
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); assert(window);
            assert(window->grabWindow().save(preview+"-real-cloud.png"));
        }
        sim.switchCloud(false);
        QFile selection(scratch.path()+"/cloud-mode"); assert(selection.open(QIODevice::ReadOnly));
        assert(selection.readAll()=="mock");
        std::cout<<"Real cloud routing, TLS, credential forwarding, downloads, profile isolation and reader guards passed.\n";
        return 0;
    }
    control.initialize(); finish(control); assert(control.initialized() && !control.signedIn());
    sim.network(1); control.signIn("demo@example.test","demo"); finish(control);
    assert(!control.signedIn() && control.status().toStdString().find("Wi-Fi connection failed")!=std::string::npos);
    sim.network(0); control.signIn("demo@example.test","demo"); finish(control);
    if(!control.signedIn()) std::cerr<<"Sign-in failed: "<<control.status().toStdString()<<"\n";
    assert(control.signedIn());
    observer.sign_in("demo@example.test","demo",time(nullptr));
    action(control,"Refresh library");
    assert(control.library()->entries().size()==51 && control.library()->pages()==7);
    settle(1500); // Only the visible covers load after the metadata refresh completes.
    assert(!control.library()->at(0).cover.empty());
    sim.network(3); control.refreshLibrary(); assert(control.busy()); finish(control);
    assert(control.status().toStdString().find("timed out")!=std::string::npos);
    control.refreshLibrary(); assert(control.status().toStdString().find("still finishing")!=std::string::npos);
    sim.releaseNetwork(); sim.network(0); action(control,"Refresh library");
    sim.transfer(2); action(control,"Refresh library");
    assert(control.status().toStdString().find("503")!=std::string::npos && control.library()->entries().size()==51);
    sim.transfer(0);
    // A cancelled slow transfer never reaches the fixture response.
    sim.transfer(1);
    cancelled=true;
    bool cancellation_observed=false;
    try { observer.get("/api/sync?type=books&since=0",time(nullptr)); }
    catch(const std::exception& error) { cancellation_observed=std::string(error.what())=="Cancelled."; }
    cancelled=false; sim.transfer(0); assert(cancellation_observed);
    // Both transfer failures must leave no installed book or partial directory.
    pick(0);
    for(int mode:{3,4}) {
        sim.transfer(mode); action(control,"Download EPUB");
        assert(current().book.path.empty());
        assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot).empty());
    }
    sim.transfer(0); action(control,"Download EPUB");
    assert(!current().book.path.empty());
    const auto path=current().book.path;
    action(control,"Read offline"); assert(!sim.readerPath().isEmpty() && sim.chapter()==1);
    sim.closeReader(); finish(control);
    action(control,"Sync now"); assert(control.status()=="Reading positions are synchronized.");
    sim.remoteChapter(2); action(control,"Sync now"); assert(control.status().startsWith("Readest position downloaded"));
    action(control,"Open at Readest position"); assert(sim.chapter()==2 && !sim.readerPath().isEmpty());
    sim.turnReader(1); sim.closeReader(); finish(control);
    sim.remoteChapter(1); action(control,"Sync now"); assert(control.status().startsWith("Both positions differ"));
    action(control,"Use Readest position");
    action(control,"Open at Readest position"); assert(sim.chapter()==1);
    sim.turnReader(1); sim.closeReader(); finish(control);
    action(control,"Sync now"); assert(control.status().startsWith("PocketBook position uploaded"));
    assert(fetch_progress(observer,current().id.hash,time(nullptr)).location=="epubcfi(/6/4!/4/2)");
    const auto uploaded_percentage=reading_percentage("{}",fetch_progress(observer,current().id.hash,time(nullptr)).config);
    assert(uploaded_percentage>66 && uploaded_percentage<67);
    assert(database.find(scratch.path().toStdString())==0);
    assert(QFile::exists(QString::fromStdString(path)));
    // Download metadata is sufficient to recover a completed, unregistered file.
    State recovered((scratch.path()+"/recovery.db").toStdString());
    auto page=fetch_library_page(observer,0,100,time(nullptr));
    recovered.apply_page(observer.session().user_id,0,page);
    assert(recover_downloads(recovered,observer.session().user_id,books_root).empty());
    bool found=false;
    for(const auto& book:recovered.books(observer.session().user_id)) if(book.path==path) found=true;
    assert(found);
    // Reinitializing fixture services must preserve remote and native positions.
    sim.prepare();
    assert(fetch_progress(observer,current().id.hash,time(nullptr)).location=="epubcfi(/6/4!/4/2)");
    assert(readest::native_position(database,path).cfi=="epubcfi(/6/4!/4/2)");
    action(control,"Back to library");
    // Link a progress-only book offline, retaining its existing native position.
    const auto existing=QString::fromStdString(platform::bookRoots().front())+"/Existing book.epub";
    assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/01.epub",existing));
    const auto existing_hash=readest::inspect_epub(existing.toStdString()).readest_hash;
    assert(sim.open(existing)); sim.turnReader(1); sim.closeReader(); finish(control);
    const auto before=readest::native_position(database,existing.toStdString()).cfi;
    const auto managed_dirs=QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    sim.network(1); control.scanDevice(); finish(control);
    control.setAvailabilityFilter(2); assert(static_cast<size_t>(count())==2);
    for(int i=0;i<control.library()->rowCount();++i) if(control.library()->at(i).id.hash==existing_hash) {
        pick(i); break;
    }
    assert(control.detail() && current().book.path==existing.toStdString() && current().book.epubs==0);
    action(control,"Read offline"); assert(sim.readerPath()==existing);
    sim.closeReader(); finish(control);
    assert(readest::native_position(database,existing.toStdString()).cfi==before);
    assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==managed_dirs);
    action(control,"Back to library"); control.setAvailabilityFilter(0); sim.network(0);
    // A file copied after the last scan also prevents a download, even if cloud requests fail.
    control.search("04 ·"); assert(static_cast<size_t>(count())==1); pick(0);
    const auto late=QString::fromStdString(platform::bookRoots().front())+"/Copied later.epub";
    assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/03.epub",late));
    sim.transfer(2); action(control,"Download EPUB");
    assert(current().book.path==late.toStdString());
    assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==managed_dirs);
    sim.transfer(0); action(control,"Back to library"); control.search("");
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); assert(window);
    assert(!window->property("resumeOnActivation").toBool());
    auto* overlay=window->findChild<QQuickItem*>("simulatorOverlay"); assert(overlay);
    auto* panel=overlay->findChild<QQuickItem*>("simulatorPanel"); assert(panel);
    panel->setVisible(false);
    auto* libraryPage=window->findChild<QObject*>("nativeLibraryPage"); assert(libraryPage);
    assert(sim.readerPath().isEmpty());
    libraryPage->setProperty("menuOpen",true); sim.button(Qt::Key_Back); settle();
    assert(!libraryPage->property("menuOpen").toBool());
    for(int i=0;i<13;++i) control.turnPage(-1);
    sim.button(Qt::Key_PageDown); settle(); assert(control.library()->page()==2);
    const auto rotation_anchor=control.library()->at(0).id;
    const auto output=qEnvironmentVariable("READEST_UI_PREVIEW");
    for(bool wide:{false,true}) {
        if(wide) sim.rotate();
        settle();
        assert(control.library()->pages()==(wide?9:7));
        if(wide) {
            bool anchor_visible=false;
            for(int row=0;row<control.library()->rowCount();++row)
                if(control.library()->at(row).id==rotation_anchor) anchor_visible=true;
            assert(anchor_visible);
        }
        auto picture=window->grabWindow(); assert(!picture.isNull());
        if(!output.isEmpty()) assert(picture.save(output+(wide?"-simulator-landscape.png":"-simulator-portrait.png")));
    }
    panel->setVisible(true); settle();
    if(!output.isEmpty()) assert(window->grabWindow().save(output+"-simulator-controls.png"));
    // Unknown origins never reach a real network transport.
    bool blocked=false;
    try { config.transport.request("https://example.com","GET",{},"","",1000,cancelled); }
    catch(const std::exception&) { blocked=true; }
    assert(blocked && !warnings);
    const auto mockSession=root+"/session.json";
    sim.switchCloud(true);
    QFile selection(scratch.path()+"/cloud-mode"); assert(selection.open(QIODevice::ReadOnly));
    assert(selection.readAll()=="real" && QFile::exists(QString::fromStdString(mockSession)));
    std::cout<<"Simulator login, network faults, download integrity/recovery, reader handoff, bidirectional sync/conflicts and QML checks passed.\n";
    return 0;
}
