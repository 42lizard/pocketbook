#include <QTemporaryDir>
#include <QByteArray>
// Select scratch storage before the controller's file-scope paths initialize.
static QTemporaryDir scratch("/tmp/readest-simulator-test-XXXXXX");
static const bool configured=qputenv("POCKETBOOK_SIM_ROOT",scratch.path().toUtf8());
#include "controller.cpp"
#define main desktop_app_entry
#include "main.cpp"
#undef main
#include <QElapsedTimer>
#include <QQuickItem>
#include <QThread>
#include <cassert>
#include <iostream>

static void settle(int milliseconds=100) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static void finish() {
    QElapsedTimer timer; timer.start();
    while(busy && timer.elapsed()<15000) settle(10);
    if(busy) std::cerr<<"Timed out: "<<busy_message<<"\n";
    assert(!busy);
}
static void action(AppController& control,const std::string& label) {
    for(size_t i=0;i<buttons.size();++i) if(buttons[i].first==label) { control.activate(i); finish(); return; }
    std::cerr<<"Missing action: "<<label<<"; status: "<<status<<"\n";
    assert(false);
}

int main(int argc,char** argv) {
    assert(configured && scratch.isValid());
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
    Simulator sim; sim.prepare();
    AppController control;
    QQmlApplicationEngine engine;
    engine.addImportPath(READEST_TEST_CONTROLS);
    engine.addImageProvider("cover",new CoverProvider);
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
        control.initialize(); finish(); assert(initialized && !signed_in);
        assert(root==scratch.path().toStdString()+"/real-cloud/system/readest-sync");
        const auto url=qEnvironmentVariable("SIM_TEST_URL").toStdString();
        const auto certificate=qEnvironmentVariable("SIM_TEST_CA").toStdString();
        assert(!url.empty() && !certificate.empty());
        Cloud local(root+"/local-test-session.json",certificate,"test-key",url,url);
        sim.transfer(2); // Mock fault controls cannot replace real requests.
        local.sign_in("demo+ä@example.test","Test@+ü!#",time(nullptr));
        assert(local.session().user_id=="local-test-user");
        assert(fetch_library_page(local,0,100,time(nullptr)).books.empty());
        const auto downloaded=root+"/transport-download";
        const int fd=::open(downloaded.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600); assert(fd>=0);
        auto response=https_download(url+"/download",fd,certificate,100); ::close(fd);
        assert(response.status==200 && response.bytes==18);
        bool rejected=false;
        try { https_request(url,"GET",{},"",root+"/missing-ca",100); }
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
    control.initialize(); finish(); assert(initialized && !signed_in);
    sim.network(1); control.signIn("demo@example.test","demo"); finish();
    assert(!signed_in && status.find("Wi-Fi connection failed")!=std::string::npos);
    sim.network(0); control.signIn("demo@example.test","demo"); finish();
    if(!signed_in) std::cerr<<"Sign-in failed: "<<status<<"\n";
    assert(signed_in);
    action(control,"Refresh library");
    assert(library.size()==51 && control.view()["pages"].toInt()==9);
    assert(!control.view()["books"].toList()[0].toMap()["cover"].toString().isEmpty());
    sim.network(3); control.activate(0); assert(connecting && connection_pending);
    connection_deadline=Clock::now()-std::chrono::seconds(1); finish();
    assert(status.find("timed out")!=std::string::npos && connection_pending);
    control.activate(0); assert(status.find("still finishing")!=std::string::npos);
    sim.releaseNetwork(); sim.network(0); action(control,"Refresh library");
    sim.transfer(2); action(control,"Refresh library");
    assert(status.find("503")!=std::string::npos && library.size()==51);
    sim.transfer(0);
    // A cancelled slow transfer never reaches the fixture response.
    sim.transfer(1);
    std::atomic<bool> cancelled{true};
    set_http_cancellation(&cancelled);
    bool cancellation_observed=false;
    try { cloud->get("/api/sync?type=books&since=0",time(nullptr)); }
    catch(const std::exception& error) { cancellation_observed=std::string(error.what())=="Cancelled."; }
    set_http_cancellation(nullptr); sim.transfer(0); assert(cancellation_observed);
    // Both transfer failures must leave no installed book or partial directory.
    control.activate(2);
    for(int mode:{3,4}) {
        sim.transfer(mode); action(control,"Download EPUB");
        assert(library[selected_book].path.empty());
        assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot).empty());
    }
    sim.transfer(0); action(control,"Download EPUB");
    assert(!library[selected_book].path.empty());
    const auto path=library[selected_book].path;
    action(control,"Read offline"); assert(!sim.readerPath().isEmpty() && sim.chapter()==1);
    sim.closeReader(); finish();
    action(control,"Sync now"); assert(last_action==SyncAction::EstablishBaseline);
    sim.remoteChapter(2); action(control,"Sync now"); assert(last_action==SyncAction::ApplyRemote);
    action(control,"Open at Readest position"); assert(sim.chapter()==2 && !sim.readerPath().isEmpty());
    sim.turnReader(1); sim.closeReader(); finish();
    sim.remoteChapter(1); action(control,"Sync now"); assert(last_action==SyncAction::Conflict);
    action(control,"Use Readest position");
    action(control,"Open at Readest position"); assert(sim.chapter()==1);
    sim.turnReader(1); sim.closeReader(); finish();
    action(control,"Sync now"); assert(last_action==SyncAction::Upload);
    assert(fetch_progress(*cloud,library[selected_book].book.hash,time(nullptr)).location=="epubcfi(/6/4!/4/2)");
    assert(database.find(scratch.path().toStdString())==0);
    assert(QFile::exists(QString::fromStdString(path)));
    // Download metadata is sufficient to recover a completed, unregistered file.
    State recovered((scratch.path()+"/recovery.db").toStdString());
    auto page=fetch_library_page(*cloud,0,100,time(nullptr));
    recovered.apply_page(cloud->session().user_id,0,page);
    assert(recover_downloads(recovered,cloud->session().user_id,books_root).empty());
    bool found=false;
    for(const auto& book:recovered.books(cloud->session().user_id)) if(book.path==path) found=true;
    assert(found);
    // Reinitializing fixture services must preserve remote and native positions.
    sim.prepare();
    assert(fetch_progress(*cloud,library[selected_book].book.hash,time(nullptr)).location=="epubcfi(/6/4!/4/2)");
    assert(readest::native_position(database,path).cfi=="epubcfi(/6/4!/4/2)");
    action(control,"Back to library");
    // Link a progress-only book offline, retaining its existing native position.
    const auto existing=QString::fromStdString(platform::bookRoots().front())+"/Existing book.epub";
    assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/01.epub",existing));
    const auto existing_hash=readest::inspect_epub(existing.toStdString()).readest_hash;
    assert(sim.open(existing)); sim.turnReader(1); sim.closeReader(); finish();
    const auto before=readest::native_position(database,existing.toStdString()).cfi;
    const auto managed_dirs=QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    sim.network(1); control.scanDevice(); finish();
    control.setAvailabilityFilter(2); assert(visible.size()==2);
    for(size_t i=0;i<visible.size();++i) if(library[visible[i]].book.hash==existing_hash) {
        control.activate(static_cast<int>(2+i)); break;
    }
    assert(detail && library[selected_book].path==existing.toStdString() && library[selected_book].epubs==0);
    action(control,"Read offline"); assert(sim.readerPath()==existing);
    sim.closeReader(); finish();
    assert(readest::native_position(database,existing.toStdString()).cfi==before);
    assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==managed_dirs);
    action(control,"Back to library"); control.setAvailabilityFilter(0); sim.network(0);
    // A file copied after the last scan also prevents a download, even if cloud requests fail.
    control.search("04 ·"); assert(visible.size()==1); control.activate(2);
    const auto late=QString::fromStdString(platform::bookRoots().front())+"/Copied later.epub";
    assert(QFile::copy(QStringLiteral(READEST_SIM_FIXTURES)+"/03.epub",late));
    sim.transfer(2); action(control,"Download EPUB");
    assert(library[selected_book].path==late.toStdString());
    assert(QDir(QString::fromStdString(books_root)).entryList(QDir::Dirs|QDir::NoDotAndDotDot)==managed_dirs);
    sim.transfer(0); action(control,"Back to library"); control.search("");
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); assert(window);
    assert(!window->property("resumeOnActivation").toBool());
    auto* overlay=window->findChild<QQuickItem*>("simulatorOverlay"); assert(overlay);
    auto* panel=overlay->findChild<QQuickItem*>("simulatorPanel"); assert(panel);
    panel->setVisible(false);
    const auto output=qEnvironmentVariable("READEST_UI_PREVIEW");
    for(bool wide:{false,true}) {
        if(wide) sim.rotate();
        settle();
        assert(control.view()["pages"].toInt()==(wide?13:9));
        for(int i=0;i<13;++i) control.turnPage(1);
        settle();
        auto picture=window->grabWindow(); assert(!picture.isNull());
        if(!output.isEmpty()) assert(picture.save(output+(wide?"-simulator-landscape.png":"-simulator-portrait.png")));
    }
    panel->setVisible(true); settle();
    if(!output.isEmpty()) assert(window->grabWindow().save(output+"-simulator-controls.png"));
    // Unknown origins never reach a real network transport.
    bool blocked=false;
    try { https_request("https://example.com","GET",{},"","",1000); }
    catch(const std::exception&) { blocked=true; }
    assert(blocked && !warnings);
    const auto mockSession=root+"/session.json";
    sim.switchCloud(true);
    QFile selection(scratch.path()+"/cloud-mode"); assert(selection.open(QIODevice::ReadOnly));
    assert(selection.readAll()=="real" && QFile::exists(QString::fromStdString(mockSession)));
    std::cout<<"Simulator login, network faults, download integrity/recovery, reader handoff, bidirectional sync/conflicts and QML checks passed.\n";
    return 0;
}
