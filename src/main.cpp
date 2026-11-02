#include "Log.h"
#include "MainWindow.h"

#include <QApplication>

#include <curl/curl.h>

int main(int argc, char* argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Folix"));
    QApplication::setOrganizationName(QStringLiteral("Folix"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    Log::install();

    MainWindow window;
    window.show();

    // "--open <path>": started by FolixAgent for a newly connected drive.
    const QStringList args = QApplication::arguments();
    const qsizetype i = args.indexOf(QStringLiteral("--open"));
    if (i >= 0 && i + 1 < args.size()) {
        const QString path = args.at(i + 1);
        QMetaObject::invokeMethod(&window, [&window, path] { window.openExternal(path); }, Qt::QueuedConnection);
    }
    return app.exec();
}
