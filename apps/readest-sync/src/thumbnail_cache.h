#pragma once
#include <QByteArray>
#include <QString>
#include <mutex>
#include <utility>

class ThumbnailCache final {
public:
    struct Key {
        QString account;
        QString book;
        QString sourceVersion;
        int width=0;
        int height=0;
        QString format;
    };
    explicit ThumbnailCache(QString path,qint64 maxBytes=8*1024*1024)
        :path_(std::move(path)),max_bytes_(maxBytes) {}
    QByteArray get(const Key& key) noexcept;
    void put(const Key& key,const QByteArray& data) noexcept;
private:
    QString path_;
    qint64 max_bytes_;
    std::mutex mutex_;
};
