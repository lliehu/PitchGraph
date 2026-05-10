#include <QCoreApplication>
#include <QApplication>
#include <QTextStream>

#include "BatchExportRunner.h"
#include "MainWindow.h"

namespace {
QString parseBatchConfigPath(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--batch-config")) {
            if ((i + 1) < argc) {
                return QString::fromLocal8Bit(argv[i + 1]);
            }
            return QString();
        }
        if (arg.startsWith(QStringLiteral("--batch-config="))) {
            return arg.mid(QStringLiteral("--batch-config=").size());
        }
    }
    return QString();
}

bool hasBatchFlag(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--batch-config") || arg.startsWith(QStringLiteral("--batch-config="))) {
            return true;
        }
    }
    return false;
}
}

int main(int argc, char *argv[]) {
    if (hasBatchFlag(argc, argv)) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName("PitchGraph");
        QCoreApplication::setApplicationVersion("0.1.0");

        const QString batchConfigPath = parseBatchConfigPath(argc, argv);
        if (batchConfigPath.isEmpty()) {
            QTextStream(stderr) << "Missing value for --batch-config\n";
            return 1;
        }

        BatchExportRunner runner;
        QString errorMessage;
        if (!runner.runFromConfig(batchConfigPath, &errorMessage)) {
            QTextStream(stderr) << "Batch export failed: " << errorMessage << "\n";
            return 1;
        }

        QTextStream(stdout) << "Batch export completed successfully.\n";
        return 0;
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("PitchGraph");
    QCoreApplication::setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return app.exec();
}
