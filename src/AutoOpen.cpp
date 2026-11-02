#include "AutoOpen.h"

#include "Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include <unistd.h>

namespace {

const char* kLabel = "com.umid.folix.agent";

QString plistPath()
{
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents/") + QString::fromLatin1(kLabel)
        + QStringLiteral(".plist");
}

QString agentPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/FolixAgent");
}

QString plistContent()
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "    <key>Label</key>\n    <string>%1</string>\n"
               "    <key>ProgramArguments</key>\n    <array>\n        <string>%2</string>\n    </array>\n"
               "    <key>RunAtLoad</key>\n    <true/>\n"
               "    <key>KeepAlive</key>\n    <true/>\n"
               "    <key>ProcessType</key>\n    <string>Background</string>\n"
               "    <key>LimitLoadToSessionType</key>\n    <string>Aqua</string>\n"
               "</dict>\n</plist>\n")
        .arg(QString::fromLatin1(kLabel), agentPath().toHtmlEscaped());
}

QString domain()
{
    return QStringLiteral("gui/") + QString::number(getuid());
}

QString ignoredFile()
{
    return AutoOpen::supportDir() + QStringLiteral("/ignored-volumes.txt");
}

QStringList readIgnored()
{
    QFile f(ignoredFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

void writeIgnored(const QStringList& list)
{
    QSaveFile f(ignoredFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(list.join(QLatin1Char('\n')).toUtf8() + '\n');
        f.commit();
    }
}

} // namespace

namespace AutoOpen {

QString supportDir()
{
    const QString dir = QDir::homePath() + QStringLiteral("/Library/Application Support/Folix");
    QDir().mkpath(dir);
    return dir;
}

QString socketPath()
{
    return supportDir() + QStringLiteral("/folix.sock");
}

bool isEnabled()
{
    return QFile::exists(plistPath());
}

void setEnabled(bool on)
{
    Tools::run(QStringLiteral("/bin/launchctl"),
               {QStringLiteral("bootout"), domain() + QLatin1Char('/') + QString::fromLatin1(kLabel)}, 10000);
    if (!on) {
        QFile::remove(plistPath());
        return;
    }
    QDir().mkpath(QDir::homePath() + QStringLiteral("/Library/LaunchAgents"));
    QSaveFile f(plistPath());
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(plistContent().toUtf8());
    if (!f.commit())
        return;
    Tools::run(QStringLiteral("/bin/launchctl"), {QStringLiteral("bootstrap"), domain(), plistPath()}, 10000);
}

void syncOnStartup()
{
    // Only an installed copy registers itself, never a build folder.
    if (!QCoreApplication::applicationDirPath().startsWith(QLatin1String("/Applications/"))
        || !QFile::exists(agentPath()))
        return;
    QSettings s;
    if (!s.value(QStringLiteral("autoOpen/initialized")).toBool()) {
        s.setValue(QStringLiteral("autoOpen/initialized"), true);
        setEnabled(true);
        return;
    }
    if (!isEnabled())
        return;
    QFile f(plistPath());
    const bool current = f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()) == plistContent();
    const bool loaded = Tools::run(QStringLiteral("/bin/launchctl"),
                                   {QStringLiteral("print"), domain() + QLatin1Char('/') + QString::fromLatin1(kLabel)},
                                   10000).code == 0;
    if (!current || !loaded)
        setEnabled(true);
}

QString volumeUuid(const QString& mountPoint)
{
    const auto r = Tools::run(QStringLiteral("/usr/sbin/diskutil"), {QStringLiteral("info"), mountPoint}, 15000);
    static const QRegularExpression re(QStringLiteral("Volume UUID:\\s+([0-9A-Fa-f-]{36})"));
    const auto m = re.match(QString::fromUtf8(r.out));
    return m.hasMatch() ? m.captured(1).toUpper() : QString();
}

void ignoreVolume(const QString& uuid)
{
    if (uuid.isEmpty())
        return;
    QStringList list = readIgnored();
    if (!list.contains(uuid)) {
        list << uuid;
        writeIgnored(list);
    }
}

void clearIgnored()
{
    QFile::remove(ignoredFile());
}

int ignoredCount()
{
    return int(readIgnored().size());
}

} // namespace AutoOpen
