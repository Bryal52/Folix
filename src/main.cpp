#include "Log.h"
#include "MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

#include <curl/curl.h>

int main(int argc, char* argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Folix"));
    QApplication::setOrganizationName(QStringLiteral("Folix"));
    QApplication::setApplicationVersion(QStringLiteral(FOLIX_VERSION));
    Log::install();
#ifndef Q_OS_MACOS
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));   // the Mac takes it from the bundle
#endif

    MainWindow window;
    window.show();

    // "--open <path>": started by FolixAgent for a newly connected drive.
    const QStringList args = QApplication::arguments();
    const qsizetype i = args.indexOf(QStringLiteral("--open"));
    if (i >= 0 && i + 1 < args.size()) {
        const QString path = args.at(i + 1);
        QMetaObject::invokeMethod(&window, [&window, path] { window.openExternal(path); }, Qt::QueuedConnection);
    }
    // Used by the release build on GitHub: start, show the window, quit. Exits hard so
    // background scans cannot hold the process.
    if (args.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(5000, &app, [] {
            std::printf("Folix %s started fine\n", FOLIX_VERSION);
            std::fflush(stdout);
            std::_Exit(0);
        });
    }
    return app.exec();
}
