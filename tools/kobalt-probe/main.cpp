#include "publication/publication.h"
#include "publication/zip_resource.h"
#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#ifndef KOBALT_PROBE_HOST
#include <inkview.h>
#endif

// A build/input prerequisite check, not the Kobalt reader or a device installer.
int main(int argc, char **argv) {
#ifndef KOBALT_PROBE_HOST
    qputenv("QT_PLUGIN_PATH", "/ebrmain/plugins");
    qputenv("QT_QPA_PLATFORM", "pocketbook2");
    QCoreApplication::setSetuidAllowed(true);
    InitInkview(TASK_MAKEACTIVE);
#endif
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    QWidget window;
    QVBoxLayout layout(&window);
    QLabel message("Kobalt compatibility probe\nQt Widgets + unchanged publication code\nThis is not the EPUB reader.");
    message.setWordWrap(true);
    layout.addWidget(&message);
    QPushButton close("Close");
    layout.addWidget(&close);
    QObject::connect(&close, &QPushButton::clicked, &app, &QApplication::quit);

    if (app.arguments().contains("--self-test")) {
        QFile book(app.arguments().value(2));
        QString error;
        if (!book.open(QIODevice::ReadOnly)) return 1;
        kobalt::Publication reader;
        if (!reader.inspect(book, &error) || !reader.names().contains("c1.xhtml") || reader.protectedBook()) {
            qCritical("Publication inspection failed: %s", qPrintable(error));
            return 2;
        }
        kobalt::ZipResource chapter;
        if (!chapter.open(book, "c1.xhtml") || !chapter.next().contains("A reader keeps")) return 5;
        kobalt::Publication invalid;
        if (invalid.inspect(QByteArray("not a ZIP"), &error)) return 3;
        window.resize(600, 800);
        window.show();
        app.processEvents();
        return window.grab().isNull() ? 4 : 0;
    }
    if (app.arguments().size() > 1) {
        QFile book(app.arguments().at(1));
        kobalt::Publication publication;
        QString error;
        if (!book.open(QIODevice::ReadOnly)) {
            message.setText(book.errorString());
        } else if (!publication.inspect(book, &error)) {
            message.setText(error);
        } else {
            message.setText(QString("Archive inspected: %1 entries\nProtected: %2\nNo rendering or reading-state changes.")
                .arg(publication.names().size()).arg(publication.protectedBook() ? "yes" : "no"));
        }
    }
#ifdef KOBALT_PROBE_HOST
    window.resize(600, 800);
#else
    window.setGeometry(0, 0, ScreenWidth(), ScreenHeight() - PanelHeight());
#endif
    window.show();
    return app.exec();
}
