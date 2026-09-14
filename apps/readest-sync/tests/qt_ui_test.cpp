// Exercise the real controller and QML with a small desktop-only controls shim.
#include "../src/controller.cpp"
#define main desktop_app_entry
#include "../src/main.cpp"
#undef main
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QQuickItem>
#include <cassert>
#include <iostream>
static void settle(int milliseconds=100) {
    QElapsedTimer timer; timer.start();
    while(timer.elapsed()<milliseconds) { QCoreApplication::processEvents(); QThread::msleep(1); }
}
static void finish() {
    QElapsedTimer timer; timer.start();
    while(busy && timer.elapsed()<5000) settle(10);
    assert(!busy);
}
static int check_progress_layout(QQuickItem* item) {
    int count=0;
    if(item->objectName()=="bookProgress") {
        assert(item->y()+item->height()<=item->parentItem()->height()+1);
        ++count;
    }
    for(auto* child:item->childItems()) count+=check_progress_layout(child);
    return count;
}
int main(int argc,char** argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
    AppController control;
    initialized=true; signed_in=false; home();
    control.signIn("",""); assert(!busy && status=="Enter both your email and password first.");
    QTemporaryDir temp;
    assert(temp.isValid());
    state.reset(new State((temp.path()+"/state.db").toStdString()));
    auto api=[](const std::string&,const std::string&,const std::vector<std::string>&,const std::string&,const std::string&,size_t) {
        HttpResponse r; r.status=500; r.body="{}"; return r;
    };
    cloud.reset(new Cloud((temp.path()+"/session.json").toStdString(),"test-ca","public","https://auth.test","https://api.test",api));
    signed_in=true; cover_user="fixture-user";
    assert(QDir().mkpath(QString::fromStdString(root)));
    for(int i=0;i<51;++i) {
        ManagedBook b; b.book.hash=std::string(30,'0')+(i<10?"0":"")+std::to_string(i);
        b.book.title="Book "+std::to_string(i); b.book.author="Example Author"; b.epubs=i%2;
        b.book.raw=i%2?R"({"progress":[15,60],"updated_at":100})":"{}";
        if(i%7==0) {
            QImage cover(904,1317,QImage::Format_Grayscale8); cover.fill(100+i);
            const auto path=QString::fromStdString(cover_path(root,cover_user,b.book.hash,b.files));
            assert(cover.save(path,i==7?"JPEG":"PNG"));
            CoverProvider provider; QSize size;
            const auto decoded=provider.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(path)),&size,QSize(324,485));
            assert(!decoded.isNull() && decoded.width()==324 && decoded.height()==472);
        }
        library.push_back(b);
    }
    home();
    assert(control.view()["books"].toList().size()==6 && control.view()["pages"].toInt()==9);
    assert(control.view()["books"].toList()[0].toMap()["readestProgress"]=="—");
    assert(control.view()["books"].toList()[1].toMap()["readestProgress"]=="25.0%");
    assert(control.view()["books"].toList()[0].toMap()["localProgress"]=="—");
    library[0].path=(temp.path()+"/local.epub").toStdString();
    { QFile f(QString::fromStdString(library[0].path)); assert(f.open(QIODevice::WriteOnly)); }
    local_percentages[library[0].path]=12.5;
    SavedSync pending; pending.pending_remote="epubcfi(/6/4!/4/2)";
    pending.remote_config=R"({"progress":[30,60],"updatedAt":200})";
    state->save_sync(cover_user,library[0].book.hash,pending);
    home();
    assert(control.view()["books"].toList()[0].toMap()["localProgress"]=="12.5%");
    assert(control.view()["books"].toList()[0].toMap()["readestProgress"]=="50.0%");
    control.activate(2); assert(buttons.front().first=="Open at Readest position");
    control.back();
    assert(state->sync(cover_user,library[0].book.hash).pending_remote==pending.pending_remote);
    control.turnPage(1); assert(page==1);
    for(int i=0;i<20;++i) control.turnPage(1);
    assert(page==8 && control.view()["books"].toList().size()==3);
    control.search("Book 50"); assert(page==0 && visible.size()==1);
    control.activate(2); assert(detail && buttons.front().first=="Check availability");
    control.back(); assert(!detail);
    control.search(""); control.setAvailabilityFilter(1);
    assert(page==0 && visible.size()==25);
    control.search("Book 1"); assert(visible.size()==6);
    control.setAvailabilityFilter(2); assert(visible.empty());
    control.search(""); assert(visible.size()==1);
    control.setAvailabilityFilter(3); assert(visible.size()==25);
    control.setAvailabilityFilter(0); assert(visible.size()==51);
    control.search(""); control.setLandscape(true);
    assert(control.view()["books"].toList().size()==4 && control.view()["pages"].toInt()==13);
    control.setLandscape(false);
    // Busy snapshots cannot expose mutable worker-owned library state.
    start("Fixture work",[]{ status="Finished"; });
    assert(control.view()["busy"].toBool() && !control.view().contains("books"));
    control.turnPage(1); assert(page==0);
    finish(); assert(status=="Finished");
    start("Fixture error",[]{ throw std::runtime_error("Fixture failure"); });
    finish(); assert(status=="Fixture failure");
    bool network_task=false;
    start("Network fixture",[&]{ network_task=true; },true);
    assert(connecting && connection_pending.load()); finish(); assert(network_task);
    connection_pending=true;
    start("Overlapping connection",[]{},true);
    assert(!busy && status.find("still finishing")!=std::string::npos);
    connection_pending=false;
    start("Cancelled connection",[]{ assert(false); },true);
    cancel=true; finish(); assert(status=="Cancelled.");
    // Malformed persisted credentials remain a recoverable sign-in state.
    { QFile f(temp.path()+"/session.json"); assert(f.open(QIODevice::WriteOnly)); f.write("{bad"); }
    restore_session(); assert(!cloud->session().signed_in());
    assert(status.find("Please sign in again")!=std::string::npos);
    // Load the actual resource and render every page in both orientations.
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
    status="Library refreshed."; home(); engine.load(QUrl("qrc:/Main.qml"));
    assert(!engine.rootObjects().isEmpty());
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
    signed_in=false; home(); settle();
    assert(window->findChild<QObject*>("emailInput"));
    assert(window->findChild<QObject*>("passwordInput"));
    assert(!warnings);
    std::cout<<"Qt controller and QML paging, search, sign-in, worker and rendering checks passed.\n";
}
