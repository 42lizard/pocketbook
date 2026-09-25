#include "simulator.h"
#include "controller.h"
#include "platform.h"
#include "integrity.h"
#include "probe.h"
#include "http.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <sqlite3.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace {
Simulator* simulator=nullptr;
bool realCloudMode() { return qEnvironmentVariable("POCKETBOOK_SIM_CLOUD","mock")=="real"; }
QString storageBase() { return pocketbook::Device::storageRoot(); }
// Keep existing mock sessions in place; real data always has its own directory.
QString storage() { return storageBase()+(realCloudMode()?"/real-cloud":""); }
QByteArray readFile(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot read simulator fixture");
    return file.readAll();
}
void writeFile(const QString& path,const QByteArray& bytes) {
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        throw std::runtime_error("Cannot save simulator data");
}
QString cfi(int chapter) { return QString("epubcfi(/6/%1!/4/2)").arg(chapter*2); }
void sql(const char* statement,const QStringList& values={}) {
    sqlite3* raw=nullptr;
    if(sqlite3_open(platform::nativeDatabase().toUtf8().constData(),&raw)!=SQLITE_OK) {
        if(raw) sqlite3_close(raw);
        throw std::runtime_error("Cannot open simulated reading database");
    }
    std::unique_ptr<sqlite3,decltype(&sqlite3_close)> db(raw,sqlite3_close);
    sqlite3_busy_timeout(raw,2000);
    sqlite3_stmt* prepared=nullptr;
    if(sqlite3_prepare_v2(raw,statement,-1,&prepared,nullptr)!=SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(raw));
    std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> query(prepared,sqlite3_finalize);
    for(int i=0;i<values.size();++i) {
        const auto bytes=values[i].toUtf8();
        if(sqlite3_bind_text(prepared,i+1,bytes.constData(),bytes.size(),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("Cannot bind simulated database value");
    }
    if(sqlite3_step(prepared)!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(raw));
}
}

namespace platform {
QSize initialize() { return {1404,1800}; }
QString fontFamily() { return "DejaVu Sans"; }
// These identify the schema being simulated, not firmware running on this host.
QString model() { return "PB743G"; }
QString firmware() { return "U743g.6.11.1683"; }
QString dataRoot() { return storage()+"/system/readest-sync"; }
QString nativeDatabase() { return storage()+"/system/explorer-3/explorer-3.db"; }
std::vector<std::string> bookRoots() { return {storage().toStdString()}; }
int wakeNetwork() { return 0; }
void keepAwake(bool) {}
int connectNetwork(int (*callback)(int)) { simulator->connectNetwork(callback);return 0; }
void pingNetwork() {}
bool networkReady() { return simulator && simulator->networkReady(); }
bool openBook(const QString& path) { return simulator && simulator->open(path); }
QImage localCover(const QString&,const QSize&) { return {}; }
}

Simulator::Simulator() { simulator=this; }
Simulator::~Simulator() { simulator=nullptr; }
bool Simulator::realCloud() const { return realCloudMode(); }
void Simulator::switchCloud(bool real) {
    if(real==realCloud() || !controller_ || controller_->busy() || !path_.isEmpty()) return;
    try {
        writeFile(storageBase()+"/cloud-mode",real?"real":"mock");
        qputenv("POCKETBOOK_SIM_CLOUD",real?"real":"mock");
        QCoreApplication::exit(42);
    } catch(const std::exception& error) { report(QString::fromUtf8(error.what())); }
}
void Simulator::prepare() {
    const auto mode=qEnvironmentVariable("POCKETBOOK_SIM_CLOUD","mock");
    if(mode!="mock" && mode!="real") throw std::runtime_error("POCKETBOOK_SIM_CLOUD must be mock or real");
    prepareStorage();
    const auto base=storage();
    QDir data(storage());
    if(!data.mkpath("system/readest-sync") || !data.mkpath("system/explorer-3") || !data.mkpath("Books/Readest"))
        throw std::runtime_error("Cannot create simulator storage");
    sql("CREATE TABLE IF NOT EXISTS folders(id INTEGER PRIMARY KEY, storageid INTEGER, name TEXT)");
    sql("CREATE TABLE IF NOT EXISTS files(book_id INTEGER PRIMARY KEY, folder_id INTEGER, storageid INTEGER, filename TEXT, fast_hash BLOB)");
    sql("CREATE TABLE IF NOT EXISTS books_settings(bookid INTEGER PRIMARY KEY, profileid INTEGER, position TEXT, position_ts INTEGER, cpage INTEGER, npage INTEGER, completed INTEGER)");
    if(realCloud()) {
        // Reuse the system trust store; never disable TLS verification.
        const auto certificates=qEnvironmentVariable("READEST_SIM_CA","/etc/ssl/certs/ca-certificates.crt");
        writeFile(platform::dataRoot()+"/ca-certificates.crt",readFile(certificates));
        report("Real Readest cloud. Sign in with your Readest account. Downloads and sessions are separate from mock mode.");
        return;
    }
    mock_=std::make_shared<MockCloud>(base,QStringLiteral(READEST_SIM_FIXTURES));
    lastHash_=mock_->firstHash();
    report("Local fixtures ready. Sign in with any nonempty email and password, then Refresh library.");
}
void Simulator::attach(QQmlApplicationEngine& engine,AppController& controller) {
    controller_=&controller;
    window_=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window_) throw std::runtime_error("Simulator could not find app window");
    window_->setTitle(realCloud()?"PocketBook simulator — Real Readest cloud":"PocketBook simulator — Mock cloud");
    window_->setProperty("signInNotice",realCloud()?"Real Readest cloud — sign in with your Readest account.":"Mock cloud — use any nonempty email/password or the demo button in Simulator.");
    // The simulated reader is an overlay in this same window. Alt-Tab must not
    // count as returning from it; closeReader delivers that event explicitly.
    window_->setProperty("resumeOnActivation",false);
    engine.rootContext()->setContextProperty("simulator",this);
    buttonHandler=[this](int key) {
        if(!path_.isEmpty()) {
            if(key==Qt::Key_Back) closeReader();
            else if(key==Qt::Key_PageUp || key==Qt::Key_PageDown) turnReader(key==Qt::Key_PageUp?-1:1);
        } else {
            QMetaObject::invokeMethod(window_,"handleHardwareButton",Q_ARG(QVariant,QVariant(key)));
        }
    };
    attachWindow(engine,QUrl("qrc:/simulator/Simulator.qml"));
}
void Simulator::transfer(int mode) { if(!realCloud() && mode>=0 && mode<=4) { mock_->transfer(mode); report("Transfer mode changed for subsequent requests."); } }
bool Simulator::open(const QString& path) {
    try {
        const auto canonical=QFileInfo(path).canonicalFilePath();
        const auto storage_root=QFileInfo(storage()).canonicalFilePath();
        if(storage_root.isEmpty() || !canonical.startsWith(storage_root+"/"))
            throw std::runtime_error("Book is outside simulator storage");
        if(realCloud()) {
            // Synthetic chapter CFIs describe our fixture EPUBs only. Never
            // manufacture native positions that could be uploaded to a real book.
            readest::inspect_epub(path.toStdString());
            path_=path; emit changed(); return true;
        }
        const auto hash=QString::fromStdString(readest::inspect_epub(path.toStdString()).readest_hash);
        const int id=mock_->bookId(hash);
        if(!id) throw std::runtime_error("Book is not a simulator fixture");
        const auto position=readest::native_position(platform::nativeDatabase().toStdString(),path.toStdString());
        chapter_=1;
        for(int i=2;i<=3;++i) if(!position.cfi.empty() && readest::compare_cfi(position.cfi,cfi(i).toStdString())>=0) chapter_=i;
        const auto number=QString::number(id);
        sql("INSERT OR REPLACE INTO folders VALUES(?,1,?)",{number,QFileInfo(path).absolutePath()});
        sql("INSERT OR REPLACE INTO files VALUES(?,?,1,?,zeroblob(16))",{number,number,QFileInfo(path).fileName()});
        path_=path; lastHash_=hash; saveChapter();
        emit changed(); return true;
    } catch(const std::exception& error) { path_.clear(); report(QString::fromUtf8(error.what())); return false; }
}
void Simulator::saveChapter() {
    sql("INSERT OR REPLACE INTO books_settings SELECT book_id,1,?,?,?,3,? FROM files WHERE filename=? AND folder_id IN (SELECT id FROM folders WHERE name=?)",
        {"pbr:/webkit?##"+cfi(chapter_),QString::number(QDateTime::currentSecsSinceEpoch()),QString::number(chapter_),chapter_==3?"1":"0",QFileInfo(path_).fileName(),QFileInfo(path_).absolutePath()});
}
void Simulator::turnReader(int direction) {
    if(realCloud() || path_.isEmpty()) return;
    try { chapter_=qBound(1,chapter_+(direction<0?-1:1),3); saveChapter(); emit changed(); }
    catch(const std::exception& error) { report(QString::fromUtf8(error.what())); }
}
void Simulator::closeReader() { path_.clear(); emit changed(); if(controller_) controller_->resume(); }
void Simulator::remoteChapter(int chapter) {
    if(realCloud() || chapter<1 || chapter>3) return;
    try {
        mock_->remoteChapter(lastHash_,chapter);
        report("Readest position changed to chapter "+QString::number(chapter)+" for the last opened book (initially book 01). Use Sync now to reconcile.");
    } catch(const std::exception& error) { report(QString::fromUtf8(error.what())); }
}

readest::HttpTransport Simulator::transport() const {
    return realCloud()?readest::https_transport():mock_->transport();
}
