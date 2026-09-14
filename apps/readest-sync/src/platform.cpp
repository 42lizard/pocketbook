#include "platform.h"
#include <cstdlib>
// Keep the legacy global macros out of the Qt controller and renderer.
#include <inkview.h>
namespace platform {
QSize initialize() { InitInkview(TASK_MAKEACTIVE); return {ScreenWidth(),ScreenHeight()-PanelHeight()}; }
QString fontFamily() { return QString::fromUtf8(iv_get_default_font(FONT_FAMILY)); }
QString model() { return QString::fromUtf8(GetDeviceModel()); }
QString firmware() { return QString::fromUtf8(GetSoftwareVersion()); }
QString dataRoot() { return QStringLiteral("/mnt/ext1/system/readest-sync"); }
QString nativeDatabase() { return QStringLiteral("/mnt/ext1/system/explorer-3/explorer-3.db"); }
void connectNetwork(int (*callback)(int)) { NetConnectAsync(callback); }
void pingNetwork() { NetMgrPing(); }
bool openBook(const QString& path) { return OpenBook(path.toUtf8().constData(),nullptr,0)!=0; }
QImage localCover(const QString& path,const QSize& size) {
    auto* bitmap=GetBookCover(path.toUtf8().constData(),size.width(),size.height());
    if(!bitmap) return {};
    QImage result;
    if(bitmap->width && bitmap->height && bitmap->width<=4096 && bitmap->height<=4096 &&
       static_cast<unsigned>(bitmap->width)*bitmap->height<=4*1024*1024 &&
       (bitmap->depth==4 || bitmap->depth==8) && bitmap->scanline>=(bitmap->width*bitmap->depth+7)/8) {
        result=QImage(bitmap->width,bitmap->height,QImage::Format_Grayscale8);
        if(!result.isNull()) for(int y=0;y<bitmap->height;++y) for(int x=0;x<bitmap->width;++x) {
            unsigned char v=bitmap->data[y*bitmap->scanline+(bitmap->depth==8?x:x/2)];
            result.scanLine(y)[x]=bitmap->depth==8?v:((x%2?v&15:v>>4)*17);
        }
    }
    std::free(bitmap); return result;
}
}
