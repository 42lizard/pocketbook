#include "cover_provider.h"
#include "controller.h"
#include "platform.h"
#include "download.h"
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickImageProvider>
#include <QImageReader>
#include <QTimer>
#include <QFileInfo>
#ifdef READEST_SIMULATOR
#include "simulator.h"
#endif

static int runApp(int argc,char** argv) {
#ifndef READEST_DESKTOP
    if(qEnvironmentVariableIsEmpty("QT_PLUGIN_PATH")) qputenv("QT_PLUGIN_PATH","/ebrmain/plugins");
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM","pocketbook2");
#endif
    QCoreApplication::setSetuidAllowed(true);
    const QSize screen=platform::initialize();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc,argv);
#ifdef READEST_SIMULATOR
    Simulator simulator;
    try { simulator.prepare(); }
    catch(const std::exception& e) { qCritical("Simulator: %s",e.what()); return 1; }
#endif
    app.setFont(QFont(platform::fontFamily()));
    QImageReader::setAllocationLimit(32);
    AppController controller;
    QQmlApplicationEngine engine;
#ifdef READEST_DESKTOP
    engine.addImportPath(QStringLiteral(READEST_TEST_CONTROLS));
#else
    engine.addImportPath("/ebrmain/qml");
#endif
    engine.addImageProvider("cover",new CoverProvider(platform::dataRoot()));
    engine.rootContext()->setContextProperty("appController",&controller);
    engine.rootContext()->setContextProperty("screenWidth",screen.width());
    engine.rootContext()->setContextProperty("screenHeight",screen.height());
    engine.load(QUrl("qrc:/Main.qml"));
    if(engine.rootObjects().isEmpty()) return 1;
#ifdef READEST_SIMULATOR
    simulator.attach(engine,controller);
#endif
    QTimer::singleShot(0,&controller,&AppController::initialize);
    return app.exec();
}

int main(int argc,char** argv) {
    const int result=runApp(argc,argv);
#ifdef READEST_SIMULATOR
    // Destroy the window, worker and VNC listener before restarting in a new mode.
    return pocketbook::Device::restartIfRequested(result,argv);
#endif
    return result;
}
