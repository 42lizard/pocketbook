#pragma once
#include "device.h"
#include <QString>

class QQmlApplicationEngine;
class QQuickWindow;
class AppController;

class Simulator : public pocketbook::Device {
    Q_OBJECT
    Q_PROPERTY(QString readerPath READ readerPath NOTIFY changed)
    Q_PROPERTY(int chapter READ chapter NOTIFY changed)
    Q_PROPERTY(bool realCloud READ realCloud CONSTANT)
public:
    Simulator();
    ~Simulator() override;
    void prepare();
    void attach(QQmlApplicationEngine&, AppController&);
    QString readerPath() const { return path_; }
    int chapter() const { return chapter_; }
    bool open(const QString& path);
    bool realCloud() const;
    Q_INVOKABLE void switchCloud(bool real);
    Q_INVOKABLE void transfer(int mode);
    Q_INVOKABLE void turnReader(int direction);
    Q_INVOKABLE void closeReader();
    Q_INVOKABLE void remoteChapter(int chapter);
private:
    void saveChapter();
    QString path_, lastHash_;
    int chapter_=1;
    AppController* controller_=nullptr;
};
