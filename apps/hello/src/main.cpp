#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#ifdef POCKETBOOK_SIMULATOR
#include "device.h"
#include <QTimer>
#else
#include <inkview.h>
#endif
static int runApp(int argc,char** argv) {
#ifndef POCKETBOOK_SIMULATOR
    qputenv("QT_PLUGIN_PATH","/ebrmain/plugins");
    qputenv("QT_QPA_PLATFORM","pocketbook2");
    QCoreApplication::setSetuidAllowed(true);
    InitInkview(TASK_MAKEACTIVE);
#endif
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
#ifdef POCKETBOOK_SIMULATOR
    pocketbook::Device device;
    try { device.prepareStorage(); }
    catch(const std::exception& e) { qCritical("Simulator: %s",e.what()); return 1; }
#endif
    QQmlApplicationEngine engine;
#ifdef POCKETBOOK_SIMULATOR
    app.setFont(QFont("DejaVu Sans"));
    engine.addImportPath(POCKETBOOK_SIM_CONTROLS);
    engine.rootContext()->setContextProperty("screenWidth",1404);
    engine.rootContext()->setContextProperty("screenHeight",1800);
#else
    app.setFont(QFont(QString::fromUtf8(iv_get_default_font(FONT_FAMILY))));
    engine.addImportPath("/ebrmain/qml");
    engine.rootContext()->setContextProperty("screenWidth",ScreenWidth());
    engine.rootContext()->setContextProperty("screenHeight",ScreenHeight()-PanelHeight());
#endif
    engine.load(QUrl("qrc:/Main.qml"));
#ifdef POCKETBOOK_SIMULATOR
    try { device.attachWindow(engine); }
    catch(const std::exception& e) { qCritical("Simulator: %s",e.what()); return 1; }
    if(app.arguments().contains("--simulator-smoke-test")) {
        QTimer::singleShot(100,&app,[&] {
            device.rotate();
            auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if(!window || window->width()!=1800 || window->height()!=1404 || window->grabWindow().isNull()) app.exit(1);
            else device.button(Qt::Key_Back);
        });
        QTimer::singleShot(5000,&app,[]{ QCoreApplication::exit(1); });
    }
#endif
    return engine.rootObjects().isEmpty()?1:app.exec();
}
int main(int argc,char** argv) { return runApp(argc,argv); }
