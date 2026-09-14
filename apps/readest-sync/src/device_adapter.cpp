#include "device_adapter.h"
#include "platform.h"
#include "public_config.h"
#include <QDir>
#include <QSaveFile>
#include <mutex>

namespace {
// InkView has one callback slot and no userdata/cancel API. Keep ownership here,
// beyond any runner's lifetime, until the outstanding callback arrives.
std::mutex network_mutex;
std::function<void(int)> pending_connection;
int network_result(int result) {
    std::function<void(int)> callback;
    { std::lock_guard<std::mutex> lock(network_mutex); callback=std::move(pending_connection); pending_connection=nullptr; }
    if(callback) callback(result);
    return 0;
}
}
readest::ApplicationConfig deviceApplicationConfig() {
    readest::ApplicationConfig config;
    config.root=platform::dataRoot().toStdString();
    config.books_root=QDir::cleanPath(QString::fromStdString(config.root)+"/../../Books/Readest").toStdString();
    config.database=platform::nativeDatabase().toStdString(); config.ca=config.root+"/ca-certificates.crt";
    config.model=platform::model().toStdString(); config.firmware=platform::firmware().toStdString();
    config.book_roots=platform::bookRoots(); config.public_key=readest::public_anon_key;
    return config;
}
DeviceAccess deviceAccess() {
    DeviceAccess access;
    access.connect=[](std::function<void(int)> callback) {
        { std::lock_guard<std::mutex> lock(network_mutex);
          if(pending_connection) return false;
          pending_connection=std::move(callback); }
        platform::connectNetwork(network_result); return true;
    };
    access.ping=[] { platform::pingNetwork(); };
    access.networkReady=[] { return platform::networkReady(); };
    access.open=[](const QString& path) { return platform::openBook(path); };
    const auto root=platform::dataRoot().toStdString();
    access.prepareCover=[root](const readest::LibraryEntry& entry) -> QString {
        if(!entry.cover.empty() || entry.availability!=readest::Availability::OnDevice) return QString::fromStdString(entry.cover);
        const auto name=readest::cover_path(root,entry.id.account,entry.id.hash,entry.book.files)+".local.png";
        if(readest::valid_cover(name)) return QString::fromStdString(name);
        // Called as deferred device work, never from model data() or QML rendering.
        const auto image=platform::localCover(QString::fromStdString(entry.book.path),QSize(400,600));
        if(image.isNull()) return {};
        QSaveFile output(QString::fromStdString(name));
        if(output.open(QIODevice::WriteOnly) && image.save(&output,"PNG") && output.commit()) return QString::fromStdString(name);
        return {};
    };
    return access;
}
