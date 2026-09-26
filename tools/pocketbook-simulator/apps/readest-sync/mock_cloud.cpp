#include "mock_cloud.h"
#include "integrity.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QThread>
#include <QUrlQuery>
#include <stdexcept>
#include <cerrno>
#include <unistd.h>
using namespace readest;
namespace {
const QString user="simulator-user";
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
QJsonObject config(const QString& hash,int chapter,qint64 timestamp) {
    return {{"user_id",user},{"book_hash",hash},{"location",cfi(chapter)},
        {"xpointer",""},{"progress",QString("[%1,3]").arg(chapter)},
        {"updated_at",timestamp}};
}

void pause(int milliseconds,const std::atomic<bool>& cancellation) {
    for(int elapsed=0;elapsed<milliseconds;elapsed+=20) {
        if(cancellation.load()) throw std::runtime_error("Cancelled.");
        QThread::msleep(20);
    }
    if(cancellation.load()) throw std::runtime_error("Cancelled.");
}
}
void MockCloud::saveRemote() {
    writeFile(root+"/remote.json",QJsonDocument(configs).toJson());
    writeFile(root+"/notes.json",QJsonDocument(notes).toJson());
    writeFile(root+"/library.json",QJsonDocument(books).toJson());
    writeFile(root+"/files.json",QJsonDocument(files).toJson());
    // Keep the library's display metadata consistent with saved remote configs.
    for(int i=0;i<books.size();++i) {
        auto book=books[i].toObject();
        const auto row=configs[book["book_hash"].toString()].toObject();
        if(row.isEmpty()) continue;
        book["progress"]=QJsonDocument::fromJson(row["progress"].toString().toUtf8()).array();
        book["updated_at"]=row["updated_at"];
        book["synced_at"]=qMax(book["synced_at"].toInteger(),row["updated_at"].toInteger());
        books[i]=book;
    }
}

MockCloud::MockCloud(QString storage,const QString& fixtures):root(std::move(storage)) {
    if(!QDir().mkpath(root)) throw std::runtime_error("Cannot create mock storage");
    books={}; files={}; objects.clear(); configs={};
    const qint64 stamp=QDateTime::currentMSecsSinceEpoch();
    for(int i=0;i<51;++i) {
        const auto path=fixtures+QString("/%1.epub").arg(i,2,10,QChar('0'));
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
            add("cover.png",readFile(fixtures+QString("/%1.png").arg(i,2,10,QChar('0'))));
        }
    }
    if(QFile::exists(root+"/library.json")) {
        const auto saved=QJsonDocument::fromJson(readFile(root+"/library.json")).array();
        for(const auto& row:saved) {
            bool exists=false;for(const auto& book:books) if(book.toObject()["book_hash"]==row.toObject()["book_hash"]) exists=true;
            if(!exists) books.append(row);
        }
    }
    if(QFile::exists(root+"/files.json")) {
        const auto saved=QJsonDocument::fromJson(readFile(root+"/files.json")).array();
        for(const auto& row:saved) {
            bool exists=false;for(const auto& file:files) if(file.toObject()["file_key"]==row.toObject()["file_key"]) exists=true;
            if(!exists) files.append(row);
        }
    }
    if(QFile::exists(root+"/remote.json")) {
        QJsonParseError error;
        const auto saved=QJsonDocument::fromJson(readFile(root+"/remote.json"),&error);
        if(error.error!=QJsonParseError::NoError || !saved.isObject()) throw std::runtime_error("Invalid simulator remote state");
        const auto rows=saved.object();
        for(auto it=rows.begin();it!=rows.end();++it) configs[it.key()]=it.value();
    }
    if(QFile::exists(root+"/notes.json")) notes=QJsonDocument::fromJson(readFile(root+"/notes.json")).object();
    saveRemote();
}
HttpResponse MockCloud::request(const std::string& address,const std::string& method,
    const std::vector<std::string>&,const std::string& body,const std::string&,size_t cap,const std::atomic<bool>& cancellation) {
    pause(transferMode.load()==1?500:20,cancellation);
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
            for(const auto& book:books) if(book.toObject()["synced_at"].toInteger()>query.queryItemValue("since").toLongLong() &&
                (query.queryItemValue("book").isEmpty() || book.toObject()["book_hash"]==query.queryItemValue("book"))) delta.append(book);
            result={{"books",delta}};
        } else if(query.queryItemValue("type")=="configs") {
            const auto row=configs.value(query.queryItemValue("book"));
            result={{"configs",row.isObject()?QJsonArray{row}:QJsonArray{}}};
        } else if(query.queryItemValue("type")=="notes") {
            QJsonArray rows;
            for(const auto& value:notes) if(value.toObject()["book_hash"]==query.queryItemValue("book")) rows.append(value);
            result={{"notes",rows}};
        } else throw std::runtime_error("Unknown simulator sync request");
    } else if(url.path()=="/api/sync" && method=="POST") {
        const auto payload=QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
        for(const auto& entry:payload["books"].toArray()) {
            const auto incoming=entry.toObject();const auto hash=incoming["hash"].toString();
            QJsonObject book{{"user_id",user},{"book_hash",hash},{"title",incoming["title"]},{"author",incoming["author"]},
                {"format","EPUB"},{"updated_at",incoming["updatedAt"]},{"synced_at",QDateTime::currentMSecsSinceEpoch()}};
            bool exists=false;for(const auto& row:books) if(row.toObject()["book_hash"]==hash) exists=true;
            if(!exists) books.append(book);
        }
        for(const auto& entry:payload["configs"].toArray()) {
            const auto incoming=entry.toObject();
            const auto hash=incoming["bookHash"].toString();
            auto row=configs[hash].toObject();row["user_id"]=user;row["book_hash"]=hash;
            row["location"]=incoming["location"]; row["xpointer"]=incoming["xpointer"];
            if(incoming["progress"].isArray())
                row["progress"]=QString::fromUtf8(QJsonDocument(incoming["progress"].toArray()).toJson(QJsonDocument::Compact));
            row["updated_at"]=incoming["updatedAt"];
            configs[hash]=row;
        }
        QJsonArray accepted;
        for(const auto& entry:payload["notes"].toArray()) {
            const auto incoming=entry.toObject();const auto key=incoming["bookHash"].toString()+"|"+incoming["id"].toString();
            auto row=notes[key].toObject();
            if(row.isEmpty() || incoming["updatedAt"].toInteger()>row["updated_at"].toInteger() ||
               incoming["deletedAt"].toInteger()>row["deleted_at"].toInteger()) {
                row={{"user_id",user},{"book_hash",incoming["bookHash"]}};
                const QMap<QString,QString> fields={{"id","id"},{"type","type"},{"cfi","cfi"},{"text","text"},{"note","note"},
                    {"color","color"},{"style","style"},{"createdAt","created_at"},{"updatedAt","updated_at"},{"deletedAt","deleted_at"}};
                for(auto field=fields.cbegin();field!=fields.cend();++field) row[field.value()]=incoming.value(field.key());
                notes[key]=row;
            }
            accepted.append(row);
        }
        saveRemote(); result={{"success",true},{"notes",accepted}};
    } else if(url.path()=="/api/storage/upload" && method=="POST") {
        const auto payload=QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
        const auto key=user+"/"+payload["fileName"].toString();
        if(!key.startsWith(user+"/Readest/Books/") || key.contains("..")) throw std::runtime_error("Invalid simulated upload key");
        bool exists=false;for(const auto& file:files) if(file.toObject()["file_key"]==key) exists=true;
        if(!exists) files.append(QJsonObject{{"book_hash",payload["bookHash"]},{"file_key",key},{"file_size",payload["fileSize"]},{"updated_at",QDateTime::currentMSecsSinceEpoch()}});
        saveRemote();result={{"uploadUrl","https://storage.simulator/"+QString::fromLatin1(QUrl::toPercentEncoding(key))}};
    } else if(url.path()=="/api/storage/list" && method=="GET") {
        QJsonArray selected;
        const auto hash=query.queryItemValue("bookHash");
        for(const auto& file:files) if(hash.isEmpty() || file.toObject()["book_hash"]==hash) selected.append(file);
        result={{"files",selected},{"page",1},{"totalPages",1}};
    } else if(url.path()=="/api/storage/download" && method=="GET") {
        const auto key=query.queryItemValue("fileKey",QUrl::FullyDecoded);
        if(!objects.contains(key) && !QFile::exists(root+"/object-"+QString::fromLatin1(QUrl::toPercentEncoding(key)))) return {404,"{}",0};
        result={{"downloadUrl","https://storage.simulator/"+QString::fromLatin1(QUrl::toPercentEncoding(key))}};
    } else throw std::runtime_error("Simulator has no fixture for this request");
    const auto bytes=json(result);
    if(bytes.size()>cap) throw std::runtime_error("Simulator response exceeds limit");
    return {200,bytes,bytes.size()};
}
HttpResponse MockCloud::download(const std::string& address,int fd,const std::string&,size_t cap,const std::atomic<bool>& cancellation) {
    const int mode=transferMode.load();
    pause(60,cancellation);
    if(mode==2) return {503,"",0};
    const QUrl url(QString::fromStdString(address));
    if(url.host()!="storage.simulator" || url.scheme()!="https")
        throw std::runtime_error("Simulator blocked an unknown download origin");
    const auto key=url.path(QUrl::FullyDecoded).mid(1);
    QFile uploaded(root+"/object-"+QString::fromLatin1(QUrl::toPercentEncoding(key)));
    if(uploaded.exists()) {
        if(!uploaded.open(QIODevice::ReadOnly) || uploaded.size()<0 || static_cast<size_t>(uploaded.size())>cap) throw std::runtime_error("Invalid simulated upload object");
        size_t total=0;
        while(!uploaded.atEnd()) {
            pause(mode==1?50:0,cancellation);const auto chunk=uploaded.read(16384);
            if(chunk.isEmpty()) throw std::runtime_error("Cannot read simulated upload");
            size_t at=0;while(at<static_cast<size_t>(chunk.size())) {
                auto n=write(fd,chunk.constData()+at,chunk.size()-at);if(n<0 && errno==EINTR) continue;
                if(n<=0) throw std::runtime_error("Cannot write simulated download");at+=n;
            }
            total+=at;
        }
        return {200,"",total};
    }
    QByteArray bytes;
    { QMutexLocker lock(&remoteMutex); if(!objects.contains(key)) return {404,"",0}; bytes=objects[key]; }
    if(static_cast<size_t>(bytes.size())>cap) throw std::runtime_error("Simulator download exceeds limit");
    if(mode==3) bytes.truncate(bytes.size()/2);
    if(mode==4 && !bytes.isEmpty()) bytes[0]='X';
    size_t at=0;
    while(at<static_cast<size_t>(bytes.size())) {
        if(cancellation.load()) throw std::runtime_error("Cancelled.");
        const auto n=write(fd,bytes.constData()+at,qMin<size_t>(mode==1?1024:16384,bytes.size()-at));
        if(n<0 && errno==EINTR) continue;
        if(n<=0) throw std::runtime_error("Cannot write simulated download");
        at+=static_cast<size_t>(n);
        // Slow mode pauses after the first chunk so cancellation exercises partial files.
        if(mode==1 && at==static_cast<size_t>(n)) pause(2500,cancellation);
    }
    return {200,"",at};
}
HttpTransport MockCloud::transport() {
    auto self=shared_from_this();
    HttpTransport result;
    result.request=[self](const std::string& url,const std::string& method,const std::vector<std::string>& headers,
        const std::string& body,const std::string& ca,size_t cap,const std::atomic<bool>& cancel) {
        return self->request(url,method,headers,body,ca,cap,cancel);
    };
    result.download=[self](const std::string& url,int fd,const std::string& ca,size_t cap,const std::atomic<bool>& cancel) {
        return self->download(url,fd,ca,cap,cancel);
    };
    result.upload=[self](const std::string& address,int fd,const std::string&,size_t size,const std::atomic<bool>& cancel) -> HttpResponse {
        pause(20,cancel);if(self->transferMode.load()==2) return {503,"",0};
        const QUrl url(QString::fromStdString(address));
        if(url.host()!="storage.simulator" || url.scheme()!="https") throw std::runtime_error("Unknown mock upload origin");
        const auto key=url.path(QUrl::FullyDecoded).mid(1);
        {QMutexLocker lock(&self->remoteMutex);bool found=false;
         for(const auto& file:self->files) if(file.toObject()["file_key"]==key && file.toObject()["file_size"].toInteger()==static_cast<qint64>(size)) found=true;
         if(!found) throw std::runtime_error("Missing simulated upload reservation");}
        QSaveFile file(self->root+"/object-"+QString::fromLatin1(QUrl::toPercentEncoding(key)));
        if(!file.open(QIODevice::WriteOnly) || lseek(fd,0,SEEK_SET)<0) throw std::runtime_error("Cannot stage simulated upload");
        char chunk[16384];size_t at=0;
        while(at<size) {
            pause(self->transferMode.load()==1?50:0,cancel);
            const auto n=read(fd,chunk,qMin(sizeof(chunk),size-at));if(n<0 && errno==EINTR) continue;
            if(n<=0 || file.write(chunk,n)!=n) throw std::runtime_error("Cannot stream simulated upload");at+=n;
        }
        if(!file.commit()) throw std::runtime_error("Cannot commit simulated upload");return {200,"",0};
    };
    return result;
}
void MockCloud::transfer(int mode) {
    if(mode<0 || mode>4) throw std::invalid_argument("Invalid mock transfer mode");
    transferMode=mode;
}
QString MockCloud::firstHash() const {
    QMutexLocker lock(&remoteMutex);
    return books[0].toObject()["book_hash"].toString();
}
int MockCloud::bookId(const QString& hash) const {
    QMutexLocker lock(&remoteMutex);
    for(int i=0;i<books.size();++i) if(books[i].toObject()["book_hash"]==hash) return i+1;
    return 0;
}
void MockCloud::remoteChapter(const QString& hash,int chapter) {
    QMutexLocker lock(&remoteMutex);
    if(chapter<1 || chapter>3 || !configs.contains(hash)) throw std::invalid_argument("Unknown mock position");
    const auto stamp=qMax(QDateTime::currentMSecsSinceEpoch(),configs[hash].toObject()["updated_at"].toInteger()+1);
    configs[hash]=config(hash,chapter,stamp); saveRemote();
}
