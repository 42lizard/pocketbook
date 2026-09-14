#pragma once
#include <QString>
#include <QSize>
#include <QImage>
#include <string>
#include <vector>
namespace platform {
QSize initialize();
QString fontFamily();
QString model();
QString firmware();
QString dataRoot();
QString nativeDatabase();
std::vector<std::string> bookRoots();
void connectNetwork(int (*callback)(int));
void pingNetwork();
bool openBook(const QString& path);
QImage localCover(const QString& path,const QSize& size);
}
