#include "device_adapter.h"
#include "platform.h"
#include "public_config.h"
#include "network_trace.h"
#include <QDir>
#include <QSaveFile>
#include <QCoreApplication>
#include <mutex>
#include <thread>

namespace {
// InkView has one callback slot and no userdata/cancel API. Keep ownership here,
// beyond any runner's lifetime, until the outstanding callback arrives.
std::mutex network_mutex;
std::function<void(int)> pending_connection;
std::atomic<bool> ping_running{false};
int network_result(int result) {
    networkTrace("connect.callback",result);
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
        networkTrace("connect.request");
        { std::lock_guard<std::mutex> lock(network_mutex);
          if(pending_connection) { networkTrace("connect.pending");return false; }
          pending_connection=std::move(callback); }
        try {
            std::thread([] {
                // WiFiPower waits for netagent too. Wake the radio first, while
                // Qt continues processing cancellation and the connection timer.
                try {
                    networkTrace("wake.begin");
                    const int status=platform::wakeNetwork();
                    networkTrace("wake.end",status);
                    if(status!=0) { network_result(status);return; }
                    if(!QMetaObject::invokeMethod(QCoreApplication::instance(),[] {
                        networkTrace("connect.begin");
                        const int result=platform::connectNetwork(network_result);
                        networkTrace("connect.return",result);
                        if(result!=0) network_result(result);
                    },Qt::QueuedConnection)) network_result(-1);
                } catch(...) { network_result(-1); }
            }).detach();
        } catch(...) { network_result(-1); }
        return true;
    };
    access.ping=[] {
        // NetMgrPing waits for netagent on device. Never wait on the Qt thread,
        // and never accumulate workers if the firmware call stops returning.
        if(ping_running.exchange(true)) return;
        try {
            std::thread([] {
                networkTrace("ping.begin");
                try { platform::pingNetwork(); } catch(...) {}
                networkTrace("ping.end");
                ping_running=false;
            }).detach(); // Process-owned call: captures no runner, UI or session.
        } catch(...) { ping_running=false; }
    };
    access.networkReady=[] { return platform::networkReady(); };
    access.keepAwake=[](bool active) {
        platform::keepAwake(active);
        networkTrace("standby.prevented",active);
    };
    access.open=[](const QString& path) { return platform::openBook(path); };
    const auto root=platform::dataRoot().toStdString();
    access.prepareCover=[root](const readest::LibraryEntry& entry) -> QString {
        if(!entry.cover.empty() || entry.availability!=readest::Availability::OnDevice) return QString::fromStdString(entry.cover);
        const auto name=readest::cover_path(root,entry.id.account,entry.id.hash,entry.book.files)+".local.png";
        if(readest::valid_cover(name)) return QString::fromStdString(name);
        // Called as deferred device work, never from model data() or QML rendering.
        const auto image=platform::localCover(QString::fromStdString(entry.book.path),QSize(400,600));
        if(image.isNull()) {
            try {
                const auto metadata=readest::epub_metadata(entry.book.path,true);
                if(metadata.cover.empty()) return {};
                QSaveFile output(QString::fromStdString(name));
                if(output.open(QIODevice::WriteOnly) && output.write(metadata.cover.data(),metadata.cover.size())==static_cast<qint64>(metadata.cover.size()) && output.commit() && readest::valid_cover(name))
                    return QString::fromStdString(name);
            } catch(const std::exception&) { /* Missing or unsupported cover is nonfatal. */ }
            return {};
        }
        QSaveFile output(QString::fromStdString(name));
        if(output.open(QIODevice::WriteOnly) && image.save(&output,"PNG") && output.commit()) return QString::fromStdString(name);
        return {};
    };
    return access;
}
