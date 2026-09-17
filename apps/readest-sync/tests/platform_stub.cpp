#include "platform.h"
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <atomic>
namespace platform_test {
std::atomic<bool> hold_ping{false},ping_entered{false},ping_finished{false};
std::atomic<int> ping_calls{0};
std::atomic<bool> sleeping_wifi{false};
std::atomic<int> wake_calls{0};
std::atomic<bool> wifi_connected{false};
}
namespace platform {
QSize initialize() { return {1404,1800}; }
QString fontFamily() { return "DejaVu Sans"; }
QString model() { return "PB743G"; }
QString firmware() { return "U743g.6.11.1683"; }
QString dataRoot() { return qEnvironmentVariable("READEST_TEST_ROOT","/tmp/readest-qt-preview/system/readest-sync"); }
QString nativeDatabase() { return dataRoot()+"/../explorer-3/explorer-3.db"; }
std::vector<std::string> bookRoots() { return {(dataRoot()+"/../..").toStdString()}; }
int connectNetwork(int (*callback)(int)) {
    if(platform_test::sleeping_wifi) return 0;
    QTimer::singleShot(0,[callback] { platform_test::wifi_connected=true;callback(0); });
    return 0;
}
int wakeNetwork() { ++platform_test::wake_calls;platform_test::sleeping_wifi=false;return 0; }
void pingNetwork() {
    ++platform_test::ping_calls;
    platform_test::ping_entered=true;
    QElapsedTimer deadline;deadline.start();
    while(platform_test::hold_ping && deadline.elapsed()<1000) QThread::msleep(1);
    platform_test::ping_finished=true;
}
void keepAwake(bool) {}
bool networkReady() { return platform_test::wifi_connected && !platform_test::sleeping_wifi; }
bool openBook(const QString&) { return true; }
QImage localCover(const QString&,const QSize&) { return {}; }
}
