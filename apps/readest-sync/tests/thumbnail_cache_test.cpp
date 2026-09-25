#include "thumbnail_cache.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <sqlite3.h>
#include <cassert>
#include <iostream>
#include <thread>

static ThumbnailCache::Key key(QString account="account",QString book="book",QString version="v1") {
    return {std::move(account),std::move(book),std::move(version),105,155,"png-v1"};
}
static long long number(const QString& path,const char* sql) {
    sqlite3* db=nullptr; sqlite3_stmt* query=nullptr;
    assert(sqlite3_open_v2(path.toUtf8().constData(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    assert(sqlite3_prepare_v2(db,sql,-1,&query,nullptr)==SQLITE_OK);
    assert(sqlite3_step(query)==SQLITE_ROW);
    const auto value=sqlite3_column_int64(query,0);
    sqlite3_finalize(query); sqlite3_close(db); return value;
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temporary; assert(temporary.isValid());
    const auto path=temporary.path()+"/cover-cache.db";
    {
        ThumbnailCache cache(path,12);
        assert(cache.get(key()).isEmpty());
        cache.put(key(),"aaaaaa");
        cache.put(key("account","other"),"bbbbbb");
        assert(cache.get(key())=="aaaaaa"); // Touch A, so B becomes least recently used.
        cache.put(key("account","third"),"cccccc");
        assert(cache.get(key())=="aaaaaa");
        assert(cache.get(key("account","other")).isEmpty());
        assert(cache.get(key("account","third"))=="cccccc");
        assert(cache.get(key("other-account","book")).isEmpty());
        assert(cache.get(key("account","book","v2")).isEmpty());
        assert(number(path,"SELECT COALESCE(SUM(bytes),0) FROM thumbnails")<=12);
    }
    { ThumbnailCache restarted(path,12); assert(restarted.get(key())=="aaaaaa"); }
    assert(QFile::remove(path));
    {
        ThumbnailCache rebuilt(path,12);
        assert(rebuilt.get(key()).isEmpty());
        rebuilt.put(key(),"fresh"); assert(rebuilt.get(key())=="fresh");
        sqlite3* lock=nullptr; assert(sqlite3_open(path.toUtf8().constData(),&lock)==SQLITE_OK);
        assert(sqlite3_exec(lock,"BEGIN EXCLUSIVE",nullptr,nullptr,nullptr)==SQLITE_OK);
        assert(rebuilt.get(key()).isEmpty()); rebuilt.put(key("account","locked"),"ignored");
        sqlite3_exec(lock,"ROLLBACK",nullptr,nullptr,nullptr); sqlite3_close(lock);
        assert(rebuilt.get(key())=="fresh");
    }
    assert(QFile::remove(path));
    { QFile corrupt(path); assert(corrupt.open(QIODevice::WriteOnly)); corrupt.write("not sqlite"); }
    { ThumbnailCache corrupt(path,12); assert(corrupt.get(key()).isEmpty()); corrupt.put(key(),"ignored"); }
    { ThumbnailCache unavailable(temporary.path()+"/missing/cover-cache.db",12); assert(unavailable.get(key()).isEmpty()); unavailable.put(key(),"ignored"); }
    if(QFile::exists("/dev/full")) {
        ThumbnailCache full("/dev/full",12); assert(full.get(key()).isEmpty()); full.put(key(),"ignored");
    }
    assert(QFile::remove(path));
    {
        ThumbnailCache concurrent(path,1024*1024);
        std::thread first([&] { for(int i=0;i<20;++i) concurrent.put(key("first",QString::number(i)),QByteArray(100,'a')); });
        std::thread second([&] { for(int i=0;i<20;++i) concurrent.put(key("second",QString::number(i)),QByteArray(100,'b')); });
        first.join(); second.join();
        for(int i=0;i<20;++i) {
            assert(concurrent.get(key("first",QString::number(i)))==QByteArray(100,'a'));
            assert(concurrent.get(key("second",QString::number(i)))==QByteArray(100,'b'));
        }
    }
    std::cout<<"Disposable thumbnail cache checks passed.\n";
}
