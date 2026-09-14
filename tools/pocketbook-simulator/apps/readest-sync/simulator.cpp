#include "simulator.h"
#include "controller.h"
#include "platform.h"
#include "integrity.h"
#include "probe.h"
#include "http.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <sqlite3.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace readest::live {
void set_http_cancellation(const std::atomic<bool>*);
HttpResponse https_request(const std::string&,const std::string&,const std::vector<std::string>&,
    const std::string&,const std::string&,size_t);
HttpResponse https_download(const std::string&,int,const std::string&,size_t);
}

namespace {
const QString user="simulator-user";
Simulator* simulator=nullptr;
std::atomic<int> transferMode{0};
thread_local const std::atomic<bool>* cancellation=nullptr;
QJsonArray books, files;
QJsonObject configs;
QMap<QString,QByteArray> objects;
QMutex remoteMutex;

bool realCloudMode() { return qEnvironmentVariable("POCKETBOOK_SIM_CLOUD","mock")=="real"; }
QString storageBase() { return pocketbook::Device::storageRoot(); }
// Keep existing mock sessions in place; real data always has its own directory.
QString storage() { return storageBase()+(realCloudMode()?"/real-cloud":""); }
std::string json(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString(); }
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
void saveRemote() {
    writeFile(storage()+"/remote.json",QJsonDocument(configs).toJson());
    // Keep the library's display metadata consistent with saved remote configs.
    for(int i=0;i<books.size();++i) {
        auto book=books[i].toObject();
        const auto row=configs[book["book_hash"].toString()].toObject();
        book["progress"]=QJsonDocument::fromJson(row["progress"].toString().toUtf8()).array();
        book["updated_at"]=row["updated_at"];
        book["synced_at"]=qMax(book["synced_at"].toInteger(),row["updated_at"].toInteger());
        books[i]=book;
    }
}
QJsonObject config(const QString& hash,int chapter,qint64 timestamp) {
    return {{"user_id",user},{"book_hash",hash},{"location",cfi(chapter)},
        {"xpointer",""},{"progress",QString("[%1,3]").arg(chapter)},
        {"updated_at",timestamp}};
}
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
void pause(int milliseconds) {
    for(int elapsed=0;elapsed<milliseconds;elapsed+=20) {
        if(cancellation && cancellation->load()) throw std::runtime_error("Cancelled.");
        QThread::msleep(20);
    }
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
void connectNetwork(int (*callback)(int)) { simulator->connectNetwork(callback); }
void pingNetwork() {}
bool openBook(const QString& path) { return simulator && simulator->open(path); }
QImage localCover(const QString&,const QSize&) { return {}; }
}

// Select one transport for the lifetime of the process. Mock requests never
// fall back to the network, including when a fixture fails or is missing.
namespace readest {
void set_http_cancellation(const std::atomic<bool>* value) { cancellation=value; live::set_http_cancellation(value); }
HttpResponse https_request(const std::string& address,const std::string& method,
    const std::vector<std::string>& headers,const std::string& body,const std::string& ca,size_t cap) {
    if(realCloudMode()) return live::https_request(address,method,headers,body,ca,cap);
    pause(transferMode.load()==1?500:20);
    if(transferMode.load()==2) return {503,"{}",0};
    QMutexLocker lock(&remoteMutex);
    const QUrl url(QString::fromStdString(address));
    const QUrlQuery query(url);
    QJsonObject result;
    if(url.host()=="readest.supabase.co" && url.path()=="/auth/v1/token" && method=="POST") {
        result={{"access_token","simulator-access"},{"refresh_token","simulator-refresh"},
            {"expires_at",QDateTime::currentSecsSinceEpoch()+3600},{"user",QJsonObject{{"id",user}}}};
    } else if(url.host()!="web.readest.com") {
        throw std::runtime_error("Simulator blocked an unknown origin");
    } else if(url.path()=="/api/sync" && method=="GET") {
        if(query.queryItemValue("type")=="books") {
            QJsonArray delta;
            for(const auto& book:books) if(book.toObject()["synced_at"].toInteger()>query.queryItemValue("since").toLongLong()) delta.append(book);
            result={{"books",delta}};
        } else if(query.queryItemValue("type")=="configs") {
            const auto row=configs.value(query.queryItemValue("book"));
            result={{"configs",row.isObject()?QJsonArray{row}:QJsonArray{}}};
        } else throw std::runtime_error("Unknown simulator sync request");
    } else if(url.path()=="/api/sync" && method=="POST") {
        const auto payload=QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
        for(const auto& entry:payload["configs"].toArray()) {
            const auto incoming=entry.toObject();
            const auto hash=incoming["bookHash"].toString();
            if(!configs.contains(hash)) throw std::runtime_error("Unknown simulated book");
            auto row=configs[hash].toObject();
            row["location"]=incoming["location"]; row["xpointer"]=incoming["xpointer"];
            row["updated_at"]=incoming["updatedAt"];
            configs[hash]=row;
        }
        saveRemote(); result={{"success",true}};
    } else if(url.path()=="/api/storage/list" && method=="GET") {
        QJsonArray selected;
        const auto hash=query.queryItemValue("bookHash");
        for(const auto& file:files) if(hash.isEmpty() || file.toObject()["book_hash"]==hash) selected.append(file);
        result={{"files",selected},{"page",1},{"totalPages",1}};
    } else if(url.path()=="/api/storage/download" && method=="GET") {
        const auto key=query.queryItemValue("fileKey",QUrl::FullyDecoded);
        if(!objects.contains(key)) return {404,"{}",0};
        result={{"downloadUrl","https://storage.simulator/"+QString::fromLatin1(QUrl::toPercentEncoding(key))}};
    } else throw std::runtime_error("Simulator has no fixture for this request");
    const auto bytes=json(result);
    if(bytes.size()>cap) throw std::runtime_error("Simulator response exceeds limit");
    return {200,bytes,bytes.size()};
}
HttpResponse https_download(const std::string& address,int fd,const std::string& ca,size_t cap) {
    if(realCloudMode()) return live::https_download(address,fd,ca,cap);
    const int mode=transferMode.load();
    pause(mode==1?2500:60);
    if(mode==2) return {503,"",0};
    const QUrl url(QString::fromStdString(address));
    if(url.host()!="storage.simulator" || url.scheme()!="https")
        throw std::runtime_error("Simulator blocked an unknown download origin");
    const auto key=url.path(QUrl::FullyDecoded).mid(1);
    QByteArray bytes;
    { QMutexLocker lock(&remoteMutex); if(!objects.contains(key)) return {404,"",0}; bytes=objects[key]; }
    if(static_cast<size_t>(bytes.size())>cap) throw std::runtime_error("Simulator download exceeds limit");
    if(mode==3) bytes.truncate(bytes.size()/2);
    if(mode==4 && !bytes.isEmpty()) bytes[0]='X';
    size_t at=0;
    while(at<static_cast<size_t>(bytes.size())) {
        if(cancellation && cancellation->load()) throw std::runtime_error("Cancelled.");
        const auto n=write(fd,bytes.constData()+at,bytes.size()-at);
        if(n<=0) throw std::runtime_error("Cannot write simulated download");
        at+=static_cast<size_t>(n);
    }
    return {200,"",at};
}
}

Simulator::Simulator() { simulator=this; }
Simulator::~Simulator() { simulator=nullptr; }
bool Simulator::realCloud() const { return realCloudMode(); }
void Simulator::switchCloud(bool real) {
    if(real==realCloud() || !controller_ || controller_->view()["busy"].toBool() || !path_.isEmpty()) return;
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
    books={}; files={}; objects.clear(); configs={};
    const qint64 stamp=QDateTime::currentMSecsSinceEpoch();
    for(int i=0;i<51;++i) {
        const auto path=QStringLiteral(READEST_SIM_FIXTURES)+QString("/%1.epub").arg(i,2,10,QChar('0'));
        const auto integrity=readest::inspect_epub(path.toStdString());
        const auto hash=QString::fromStdString(integrity.readest_hash);
        const auto title=QString("%1 · %2").arg(i+1,2,10,QChar('0')).arg(i==0?"Position playground":i%5==1?"Position only":"Sample library book");
        books.append(QJsonObject{{"user_id",user},{"book_hash",hash},{"title",title},
            {"author","Simulator fixtures"},{"format","EPUB"},{"updated_at",stamp},{"synced_at",stamp},
            {"progress",QJsonArray{1,3}}});
        configs[hash]=config(hash,1,stamp);
        const auto prefix=user+"/Readest/Books/"+hash+"/";
        auto add=[&](const QString& name,const QByteArray& data) {
            objects[prefix+name]=data;
            files.append(QJsonObject{{"book_hash",hash},{"file_key",prefix+name},{"file_size",data.size()},{"updated_at",1}});
        };
        if(i%5!=1) add("book.epub",readFile(path));
        if(i%5==2) add("duplicate.epub",readFile(path));
        if(i%4!=3) {
            QImage cover(400,600,QImage::Format_RGB32); cover.fill(i%2?QColor("#dedede"):QColor("#f4f4f4"));
            QPainter painter(&cover); painter.setPen(Qt::black);
            painter.setFont(QFont("DejaVu Sans",26));
            painter.drawRect(20,20,359,559);
            painter.drawText(QRect(35,70,330,400),Qt::AlignCenter|Qt::TextWordWrap,title);
            painter.end();
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            if(!cover.save(&buffer,i%2?"JPEG":"PNG")) throw std::runtime_error("Cannot create fixture cover");
            add("cover.png",bytes);
        }
    }
    if(QFile::exists(base+"/remote.json")) {
        QJsonParseError error;
        const auto saved=QJsonDocument::fromJson(readFile(base+"/remote.json"),&error);
        if(error.error!=QJsonParseError::NoError || !saved.isObject()) throw std::runtime_error("Invalid simulator remote state");
        const auto rows=saved.object();
        for(auto it=rows.begin();it!=rows.end();++it) if(configs.contains(it.key())) configs[it.key()]=it.value();
    }
    saveRemote();
    lastHash_=books[0].toObject()["book_hash"].toString();
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
    buttonHandler=[this,&controller](int key) {
        if(!path_.isEmpty()) {
            if(key==Qt::Key_Back) closeReader();
            else turnReader(key==Qt::Key_PageUp?-1:1);
        } else if(key==Qt::Key_Back) controller.back();
        else controller.turnPage(key==Qt::Key_PageUp?-1:1);
    };
    attachWindow(engine,QUrl("qrc:/simulator/Simulator.qml"));
}
void Simulator::transfer(int mode) { if(!realCloud() && mode>=0 && mode<=4) { transferMode=mode; report("Transfer mode changed for subsequent requests."); } }
bool Simulator::open(const QString& path) {
    try {
        const auto canonical=QFileInfo(path).canonicalFilePath();
        if(!canonical.startsWith(QFileInfo(storage()+"/Books/Readest").canonicalFilePath()+"/"))
            throw std::runtime_error("Book is outside simulator storage");
        if(realCloud()) {
            // Synthetic chapter CFIs describe our fixture EPUBs only. Never
            // manufacture native positions that could be uploaded to a real book.
            readest::inspect_epub(path.toStdString());
            path_=path; emit changed(); return true;
        }
        const auto hash=QString::fromStdString(readest::inspect_epub(path.toStdString()).readest_hash);
        int id=0;
        for(int i=0;i<books.size();++i) if(books[i].toObject()["book_hash"]==hash) id=i+1;
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
        QMutexLocker lock(&remoteMutex);
        const auto stamp=qMax(QDateTime::currentMSecsSinceEpoch(),configs[lastHash_].toObject()["updated_at"].toInteger()+1);
        configs[lastHash_]=config(lastHash_,chapter,stamp); saveRemote();
        report("Readest position changed to chapter "+QString::number(chapter)+" for the last opened book (initially book 01). Use Sync now to reconcile.");
    } catch(const std::exception& error) { report(QString::fromUtf8(error.what())); }
}
