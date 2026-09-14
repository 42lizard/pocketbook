#pragma once
#include "application.h"
#include <QImage>
#include <QString>
#include <functional>

// Runtime dependencies are explicit and replaceable by simulator/test adapters.
struct DeviceAccess {
    std::function<bool(std::function<void(int)>)> connect;
    std::function<void()> ping;
    std::function<bool(const QString&)> open;
    std::function<QString(const readest::LibraryEntry&)> prepareCover;
    int connectionTimeoutMs=60000;
    int keepaliveMs=30000;
};
readest::ApplicationConfig deviceApplicationConfig();
DeviceAccess deviceAccess();
