#pragma once
#include "download.h"
#include "thumbnail_cache.h"
#include <QBuffer>
#include <QFileInfo>
#include <QQuickImageProvider>
#include <QImageReader>
#include <QUrl>
class CoverProvider final : public QQuickImageProvider {
public:
    explicit CoverProvider(QString root):QQuickImageProvider(QQuickImageProvider::Image),root_(std::move(root)),cache_(root_+"/cover-cache.db") {}
    QImage requestImage(const QString& id,QSize* size,const QSize& requested) override {
        const bool thumbnail=id.startsWith("thumb/");
        const auto decoded=QUrl::fromPercentEncoding((thumbnail?id.mid(6):id).toUtf8());
        QString account,book,path=decoded;
        if(thumbnail) {
            const auto first=decoded.indexOf('\n'),second=decoded.indexOf('\n',first+1);
            if(first<0 || second<0) return {};
            account=decoded.left(first); book=decoded.mid(first+1,second-first-1); path=decoded.mid(second+1);
            if(account.size()>256 || book.size()!=32 || path.contains('\n')) return {};
            for(const auto character:book)
                if((character<'0' || character>'9') && (character<'a' || character>'f')) return {};
        }
        if(!path.startsWith(root_+"/cover-") || path.contains("/../") ||
           !readest::valid_cover(path.toStdString())) return {};
        const QSize limit(qBound(1,requested.width(),1024),qBound(1,requested.height(),1024));
        ThumbnailCache::Key key;
        if(thumbnail) {
            const QFileInfo source(path);
            key={account,book,source.fileName()+":"+QString::number(source.size())+":"+
                QString::number(source.lastModified().toMSecsSinceEpoch()),limit.width(),limit.height(),"png-v1"};
            const auto cached=QImage::fromData(cache_.get(key),"PNG");
            if(!cached.isNull() && cached.width()<=limit.width() && cached.height()<=limit.height()) {
                if(size) *size=cached.size();
                return cached;
            }
        }
        QImageReader reader(path);
        reader.setDecideFormatFromContent(true); // Readest also stores JPEG as cover.png.
        const auto original=reader.size();
        if(!original.isValid() || original.width()>4096 || original.height()>4096 ||
           qint64(original.width())*original.height()>4*1024*1024) return {};
        const auto target=original.scaled(limit,Qt::KeepAspectRatio);
        reader.setScaledSize(target);
        auto image=reader.read();
        if(image.size()!=target) image=image.scaled(target,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        if(thumbnail && !image.isNull()) {
            QByteArray encoded;
            QBuffer output(&encoded);
            if(output.open(QIODevice::WriteOnly) && image.save(&output,"PNG")) cache_.put(key,encoded);
        }
        if(size) *size=original;
        return image;
    }
private:
    QString root_;
    ThumbnailCache cache_;
};
