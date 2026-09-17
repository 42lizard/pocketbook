#include "device.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <stdexcept>
#include <unistd.h>

namespace pocketbook {
Device::Device(QObject* parent):QObject(parent) {}
QString Device::storageRoot() {
    return QDir::cleanPath(qEnvironmentVariable("POCKETBOOK_SIM_ROOT",
        "/tmp/pocketbook-simulator-"+QString::number(getpid())));
}
void Device::prepareStorage() {
    const auto root=storageRoot();
    if(!QDir::isAbsolutePath(root) || root=="/" || root.startsWith("/mnt/") || root.startsWith("/Volumes/"))
        throw std::runtime_error("Simulator storage must be a dedicated scratch directory");
    QDir dir(root);
    if(dir.exists() && !dir.exists(".pocketbook-simulator") && !dir.exists(".readest-simulator") &&
        !dir.entryList(QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden).isEmpty())
        throw std::runtime_error("Refusing a nonempty directory without a simulator marker");
    if(!dir.mkpath(".")) throw std::runtime_error("Cannot create simulator storage");
    QSaveFile marker(root+"/.pocketbook-simulator");
    if(!marker.open(QIODevice::WriteOnly) || marker.write("PocketBook simulator\n")<0 || !marker.commit())
        throw std::runtime_error("Cannot mark simulator storage");
}
int Device::restartIfRequested(int result,char** argv) {
    if(result!=42) return result;
    execvp(argv[0],argv);
    qCritical("Could not restart simulator"); return 1;
}
void Device::attachWindow(QQmlApplicationEngine& engine,const QUrl& url) {
    if(engine.rootObjects().isEmpty()) throw std::runtime_error("Simulator app has no root window");
    window_=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if(!window_) throw std::runtime_error("Simulator app must expose a QQuickWindow");
    engine.rootContext()->setContextProperty("simDevice",this);
    QQmlComponent component(&engine,url);
    auto* overlay=qobject_cast<QQuickItem*>(component.create());
    if(!overlay) throw std::runtime_error(component.errorString().toStdString());
    overlay->setParent(window_->contentItem()); overlay->setParentItem(window_->contentItem());
}
void Device::report(const QString& text) { message_=text; emit changed(); }
void Device::network(int mode) {
    if(mode>=0 && mode<=3) { networkMode_=mode; networkReady_=false;report("Wi-Fi mode changed for the next connection."); }
}
void Device::connectNetwork(int (*callback)(int)) {
    if(networkMode_==3) { pendingCallback_=callback; return; }
    const int mode=networkMode_;
    QTimer::singleShot(mode==2?5000:150,this,[this,callback,mode] { networkReady_=mode!=1;callback(mode==1?-1:0); });
}
void Device::releaseNetwork() {
    if(pendingCallback_) { auto callback=pendingCallback_; pendingCallback_=nullptr;networkReady_=true;callback(0); report("Held Wi-Fi callback released."); }
}
void Device::button(int key) {
    if(buttonHandler) { buttonHandler(key); return; }
    if(!window_) return;
    QKeyEvent press(QEvent::KeyPress,key,Qt::NoModifier),release(QEvent::KeyRelease,key,Qt::NoModifier);
    QCoreApplication::sendEvent(window_,&press); QCoreApplication::sendEvent(window_,&release);
}
void Device::rotate() { if(window_) window_->resize(window_->height(),window_->width()); }
void Device::screenshot() {
    const auto path=storageRoot()+"/screenshot-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".png";
    if(window_) report(window_->grabWindow().save(path)?"Saved "+path:"Screenshot failed.");
}
}
