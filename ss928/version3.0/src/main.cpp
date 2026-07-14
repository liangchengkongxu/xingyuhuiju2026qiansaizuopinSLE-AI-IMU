#include <QApplication>
#include <QFont>
#include <QCoreApplication>

#include "ui_common.h"
#include "main_window.h"

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_FB_HIDECURSOR", "1");

    QApplication app(argc, argv);
    app.setStyleSheet(GLOBAL_STYLE);

    QFont font = app.font();
    font.setPointSize(12);
    app.setFont(font);

    MainWindow w;
    w.showFullScreen();
    return app.exec();
}
