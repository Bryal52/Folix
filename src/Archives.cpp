#include "Archives.h"

#include "../third_party/Packix/core/Formats.h"
#include "../third_party/Packix/core/Temp.h"
#include "../third_party/Packix/core/Volumes.h"
#include "../third_party/Packix/dialogs/Lang.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>

namespace {

// The name a running Packix listens on (Packix's app/main.cpp).
QString packixInstanceName()
{
    return QStringLiteral("Packix-") + qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
}

bool sendToRunningPackix(const QString& mode, const QStringList& files)
{
    QLocalSocket socket;
    socket.connectToServer(packixInstanceName());
    if (!socket.waitForConnected(300))
        return false;
    QJsonObject request;
    request.insert(QStringLiteral("mode"), mode);
    request.insert(QStringLiteral("files"), QJsonArray::fromStringList(files));
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(2000);
    socket.disconnectFromServer();
    return true;
}

#ifdef Q_OS_MACOS
bool packixRunning()
{
    return QProcess::execute(QStringLiteral("/usr/bin/pgrep"), {QStringLiteral("-f"), QStringLiteral("Packix.app/Contents/MacOS/Packix")}) == 0;
}
#endif

} // namespace

namespace Archives {

void init()
{
    packix::ui::Lang::instance().setLanguage(QStringLiteral("ru"));
    packix::Temp::cleanupStale();
}

void shutdown()
{
    packix::Temp::cleanupSession();
}

bool enabled()
{
    return QSettings().value(QStringLiteral("archives/enabled"), true).toBool();
}

void setEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("archives/enabled"), on);
}

bool preferPackixApp()
{
    return QSettings().value(QStringLiteral("archives/usePackixApp"), true).toBool();
}

void setPreferPackixApp(bool on)
{
    QSettings().setValue(QStringLiteral("archives/usePackixApp"), on);
}

bool useApp()
{
    return enabled() && preferPackixApp() && packixInstalled();
}

bool isArchive(const QString& fileName)
{
    if (!enabled())
        return false;
    const packix::Format f = packix::Formats::instance().detect(fileName);
    if (f.isValid())
        return f.canOpen && f.defaultHandler;   // zip, 7z, rar, tar... (not apk, iso, dmg)
    // Further parts of multi-volume archives: Игра.7z.002, Игра.part2.rar, Игра.r01, Игра.z01
    static const QRegularExpression part(QStringLiteral("\\.(\\d{3}|part\\d+\\.rar|r\\d\\d|z\\d\\d)$"),
                                         QRegularExpression::CaseInsensitiveOption);
    return part.match(fileName).hasMatch();
}

QString packixApp()
{
#ifdef Q_OS_MACOS
    const QStringList candidates = {
        QStringLiteral("/Applications/Packix.app"),
        QDir::homePath() + QStringLiteral("/Applications/Packix.app"),
    };
#else
    const QStringList candidates = {
        QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/Programs/Packix/Packix.exe"),
        QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles")) + QStringLiteral("/Packix/Packix.exe"),
    };
#endif
    for (const QString& c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return {};
}

bool packixInstalled()
{
    return !packixApp().isEmpty();
}

void openInPackix(const QStringList& archives)
{
    const QString app = packixApp();
    if (app.isEmpty() || archives.isEmpty())
        return;
#ifdef Q_OS_MACOS
    // "Open these documents with Packix": works whether Packix is running or not.
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), QStringList{QStringLiteral("-a"), app} + archives);
#else
    QProcess::startDetached(app, archives);   // Packix passes it on if another copy is running
#endif
}

bool archiveInPackix(const QStringList& files)
{
    const QString app = packixApp();
    if (app.isEmpty() || files.isEmpty())
        return false;
    if (sendToRunningPackix(QStringLiteral("archive"), files)) {
#ifdef Q_OS_MACOS
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), app});   // to the front
#endif
        return true;
    }
#ifdef Q_OS_MACOS
    // A Packix that runs but does not answer cannot take the files; Folix shows the window instead.
    if (packixRunning())
        return false;
    QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                            QStringList{QStringLiteral("-a"), app, QStringLiteral("--args"), QStringLiteral("--archive")} + files);
#else
    QProcess::startDetached(app, QStringList{QStringLiteral("--archive")} + files);
#endif
    return true;
}

} // namespace Archives
