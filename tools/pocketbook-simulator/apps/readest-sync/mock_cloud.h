#pragma once
#include "http.h"
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <memory>

// Qt Core only: the overlay and headless callers share this explicit instance.
class MockCloud : public std::enable_shared_from_this<MockCloud> {
public:
    MockCloud(QString root, const QString& fixtures);
    readest::HttpTransport transport();
    void transfer(int mode);
    void remoteChapter(const QString& hash,int chapter);
    QString firstHash() const;
    int bookId(const QString& hash) const;
private:
    readest::HttpResponse request(const std::string&,const std::string&,const std::vector<std::string>&,
        const std::string&,const std::string&,size_t,const std::atomic<bool>&);
    readest::HttpResponse download(const std::string&,int,const std::string&,size_t,const std::atomic<bool>&);
    void saveRemote(); // Caller holds remoteMutex once published to other threads.
    QString root;
    QJsonArray books,files;
    QJsonObject configs;
    QMap<QString,QByteArray> objects;
    mutable QMutex remoteMutex;
    std::atomic<int> transferMode{0};
};
