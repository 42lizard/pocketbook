#include "thumbnail_cache.h"
#include <sqlite3.h>

namespace {
struct Database {
    sqlite3* value=nullptr;
    ~Database() { sqlite3_close(value); }
};
struct Statement {
    sqlite3_stmt* value=nullptr;
    ~Statement() { sqlite3_finalize(value); }
};
bool run(sqlite3* db,const char* sql) { return sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK; }
bool bind(sqlite3_stmt* query,int index,const QString& value) {
    const auto utf8=value.toUtf8();
    return sqlite3_bind_text(query,index,utf8.constData(),utf8.size(),SQLITE_TRANSIENT)==SQLITE_OK;
}
bool bindKey(sqlite3_stmt* query,const ThumbnailCache::Key& key) {
    return bind(query,1,key.account) && bind(query,2,key.book) && bind(query,3,key.sourceVersion) &&
        sqlite3_bind_int(query,4,key.width)==SQLITE_OK && sqlite3_bind_int(query,5,key.height)==SQLITE_OK &&
        bind(query,6,key.format);
}
bool open(Database& database,const QString& path) {
    const auto utf8=path.toUtf8();
    if(sqlite3_open_v2(utf8.constData(),&database.value,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) return false;
    sqlite3_busy_timeout(database.value,0);
    return run(database.value,
        "PRAGMA journal_mode=MEMORY; PRAGMA synchronous=OFF;"
        "CREATE TABLE IF NOT EXISTS thumbnails("
        "account TEXT NOT NULL,book TEXT NOT NULL,source_version TEXT NOT NULL,"
        "width INTEGER NOT NULL,height INTEGER NOT NULL,format TEXT NOT NULL,"
        "data BLOB NOT NULL,bytes INTEGER NOT NULL,used INTEGER NOT NULL,"
        "PRIMARY KEY(account,book,source_version,width,height,format));");
}
constexpr auto whereKey="account=?1 AND book=?2 AND source_version=?3 AND width=?4 AND height=?5 AND format=?6";
}

QByteArray ThumbnailCache::get(const Key& key) noexcept {
    try {
        if(max_bytes_<=0 || key.width<=0 || key.height<=0) return {};
        std::lock_guard lock(mutex_);
        Database database;
        if(!open(database,path_) || !run(database.value,"BEGIN IMMEDIATE")) return {};
        Statement query;
        const auto select=QStringLiteral("SELECT data FROM thumbnails WHERE ")+whereKey;
        if(sqlite3_prepare_v2(database.value,select.toUtf8().constData(),-1,&query.value,nullptr)!=SQLITE_OK ||
           !bindKey(query.value,key) || sqlite3_step(query.value)!=SQLITE_ROW) {
            run(database.value,"ROLLBACK"); return {};
        }
        const auto bytes=sqlite3_column_bytes(query.value,0);
        const auto* data=static_cast<const char*>(sqlite3_column_blob(query.value,0));
        if(!data || bytes<=0 || bytes>max_bytes_) { run(database.value,"ROLLBACK"); return {}; }
        QByteArray result(data,bytes);
        Statement touch;
        const auto update=QStringLiteral("UPDATE thumbnails SET used=(SELECT COALESCE(MAX(used),0)+1 FROM thumbnails) WHERE ")+whereKey;
        if(sqlite3_prepare_v2(database.value,update.toUtf8().constData(),-1,&touch.value,nullptr)!=SQLITE_OK ||
           !bindKey(touch.value,key) || sqlite3_step(touch.value)!=SQLITE_DONE ||
           !run(database.value,"COMMIT")) { run(database.value,"ROLLBACK"); return {}; }
        return result;
    } catch(...) { return {}; }
}

void ThumbnailCache::put(const Key& key,const QByteArray& data) noexcept {
    try {
        if(data.isEmpty() || data.size()>max_bytes_ || key.width<=0 || key.height<=0) return;
        std::lock_guard lock(mutex_);
        Database database;
        if(!open(database,path_) || !run(database.value,"BEGIN IMMEDIATE")) return;
        Statement insert;
        if(sqlite3_prepare_v2(database.value,
            "INSERT OR REPLACE INTO thumbnails(account,book,source_version,width,height,format,data,bytes,used) "
            "VALUES(?1,?2,?3,?4,?5,?6,?7,?8,(SELECT COALESCE(MAX(used),0)+1 FROM thumbnails))",
            -1,&insert.value,nullptr)!=SQLITE_OK || !bindKey(insert.value,key) ||
           sqlite3_bind_blob(insert.value,7,data.constData(),data.size(),SQLITE_TRANSIENT)!=SQLITE_OK ||
           sqlite3_bind_int64(insert.value,8,data.size())!=SQLITE_OK || sqlite3_step(insert.value)!=SQLITE_DONE) {
            run(database.value,"ROLLBACK"); return;
        }
        while(true) {
            Statement total;
            if(sqlite3_prepare_v2(database.value,"SELECT COALESCE(SUM(bytes),0) FROM thumbnails",-1,&total.value,nullptr)!=SQLITE_OK ||
               sqlite3_step(total.value)!=SQLITE_ROW) { run(database.value,"ROLLBACK"); return; }
            if(sqlite3_column_int64(total.value,0)<=max_bytes_) break;
            if(!run(database.value,
                "DELETE FROM thumbnails WHERE rowid=(SELECT rowid FROM thumbnails "
                "ORDER BY used,account,book,source_version,width,height,format LIMIT 1)")) {
                run(database.value,"ROLLBACK"); return;
            }
        }
        if(!run(database.value,"COMMIT")) run(database.value,"ROLLBACK");
    } catch(...) {}
}
