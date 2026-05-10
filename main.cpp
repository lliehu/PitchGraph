#include <QApplication>
#include <QCoreApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("PitchGraph");
    QCoreApplication::setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
