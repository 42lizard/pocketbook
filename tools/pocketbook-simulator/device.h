#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

class QQmlApplicationEngine;
class QQuickWindow;

namespace pocketbook {
class Device : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString message READ message NOTIFY changed)
public:
    explicit Device(QObject* parent=nullptr);
    static QString storageRoot();
    static int restartIfRequested(int result,char** argv);
    void prepareStorage();
    void attachWindow(QQmlApplicationEngine&,const QUrl& overlay=QUrl("qrc:/pocketbook-simulator/DeviceOverlay.qml"));
    void connectNetwork(int (*callback)(int));
    QString message() const { return message_; }
    std::function<void(int)> buttonHandler;
    Q_INVOKABLE void network(int mode);
    Q_INVOKABLE void releaseNetwork();
    Q_INVOKABLE void button(int key);
    Q_INVOKABLE void rotate();
    Q_INVOKABLE void screenshot();
signals:
    void changed();
protected:
    void report(const QString& text);
    QQuickWindow* window_=nullptr;
private:
    QString message_;
    int networkMode_=0;
    int (*pendingCallback_)(int)=nullptr;
};
}
