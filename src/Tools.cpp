#include "Tools.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMutex>
#include <QProcess>
#include <QStandardPaths>

namespace Tools {

QString findTool(const QString& name)
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker lock(&mutex);
    if (cache.contains(name))
        return cache.value(name);

#ifdef Q_OS_WIN
    // Next to Folix.exe first (ffmpeg ships with the Windows build), then the usual places.
    const QStringList dirs = {
        QCoreApplication::applicationDirPath(),
        QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/Android/Sdk/platform-tools"),
    };
    QString found = QStandardPaths::findExecutable(name, dirs);
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(name);
#else
    QStringList dirs = {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/usr/local/bin"),
        QDir::homePath() + QStringLiteral("/Library/Android/sdk/platform-tools"),
    };
    dirs += qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);

    QString found;
    for (const QString& d : dirs) {
        const QFileInfo fi(QDir(d).filePath(name));
        if (fi.isFile() && fi.isExecutable()) {
            found = fi.absoluteFilePath();
            break;
        }
    }
#endif
    cache.insert(name, found);
    return found;
}

QString adb() { return findTool(QStringLiteral("adb")); }
QString ffmpeg() { return findTool(QStringLiteral("ffmpeg")); }
QString ffprobe() { return findTool(QStringLiteral("ffprobe")); }

Result run(const QString& program, const QStringList& args, int timeoutMs)
{
    Result r;
    if (program.isEmpty()) {
        r.err = "tool not found";
        return r;
    }
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(5000)) {
        r.err = p.errorString().toUtf8();
        return r;
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.timedOut = true;
    }
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    r.code = (p.exitStatus() == QProcess::NormalExit && !r.timedOut) ? p.exitCode() : -1;
    return r;
}

QString shellQuote(const QString& s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 Б").arg(bytes);
    static const char* units[] = {"КБ", "МБ", "ГБ", "ТБ"};
    double v = double(bytes);
    int u = -1;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    static const QLocale ru(QLocale::Russian);
    return ru.toString(v, 'f', v < 10 ? 1 : 0) + QLatin1Char(' ') + QString::fromUtf8(units[u]);
}

QString cacheDir(const QString& sub)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QLatin1Char('/') + sub;
    QDir().mkpath(dir);
    return dir;
}

} // namespace Tools
