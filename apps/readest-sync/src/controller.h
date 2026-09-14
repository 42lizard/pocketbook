#pragma once
#include <QObject>
#include <QVariantMap>

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap view READ view NOTIFY changed)
public:
    explicit AppController(QObject* parent=nullptr);
    ~AppController() override;
    QVariantMap view() const { return view_; }
    void publish(QVariantMap value) { view_=std::move(value); emit changed(); }
    Q_INVOKABLE void initialize();
    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE void signIn(const QString& email,const QString& password);
    Q_INVOKABLE void search(const QString& text);
    Q_INVOKABLE void turnPage(int direction);
    Q_INVOKABLE void setLandscape(bool landscape);
    Q_INVOKABLE void back();
    Q_INVOKABLE void close();
    Q_INVOKABLE void resume();
signals:
    void changed();
private:
    QVariantMap view_;
};
