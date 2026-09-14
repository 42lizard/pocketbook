#include "platform.h"
#include <QTimer>
namespace platform {
QSize initialize() { return {1404,1800}; }
QString fontFamily() { return "DejaVu Sans"; }
QString model() { return "PB743G"; }
QString firmware() { return "U743g.6.11.1683"; }
QString dataRoot() { return qEnvironmentVariable("READEST_TEST_ROOT","/tmp/readest-qt-preview/system/readest-sync"); }
QString nativeDatabase() { return dataRoot()+"/../explorer-3/explorer-3.db"; }
std::vector<std::string> bookRoots() { return {(dataRoot()+"/../..").toStdString()}; }
void connectNetwork(int (*callback)(int)) { QTimer::singleShot(0,[callback] { callback(0); }); }
void pingNetwork() {}
bool networkReady() { return false; }
bool openBook(const QString&) { return true; }
QImage localCover(const QString&,const QSize&) { return {}; }
}
