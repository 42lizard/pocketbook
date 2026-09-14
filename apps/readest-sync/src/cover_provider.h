#pragma once
#include "download.h"
#include <QQuickImageProvider>
#include <QImageReader>
#include <QUrl>
class CoverProvider final : public QQuickImageProvider {
public:
    explicit CoverProvider(QString root):QQuickImageProvider(QQuickImageProvider::Image),root_(std::move(root)) {}
    QImage requestImage(const QString& id,QSize* size,const QSize& requested) override {
        const auto path=QUrl::fromPercentEncoding(id.toUtf8());
        if(!path.startsWith(root_+"/cover-") || path.contains("/../") ||
           !readest::valid_cover(path.toStdString())) return {};
        QImageReader reader(path);
        reader.setDecideFormatFromContent(true); // Readest also stores JPEG as cover.png.
        const auto original=reader.size();
        if(!original.isValid() || original.width()>4096 || original.height()>4096 ||
           qint64(original.width())*original.height()>4*1024*1024) return {};
        const QSize limit(qBound(1,requested.width(),1024),qBound(1,requested.height(),1024));
        const auto target=original.scaled(limit,Qt::KeepAspectRatio);
        reader.setScaledSize(target);
        auto image=reader.read();
        if(image.size()!=target) image=image.scaled(target,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        if(size) *size=original;
        return image;
    }
private:
    QString root_;
};

