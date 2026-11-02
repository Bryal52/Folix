#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Tools {

// Finds a command-line tool even when the app is started from Finder (minimal PATH).
QString findTool(const QString& name);
QString adb();
QString ffmpeg();
QString ffprobe();

struct Result {
    int code = -1;
    bool timedOut = false;
    QByteArray out;
    QByteArray err;
};
Result run(const QString& program, const QStringList& args, int timeoutMs = 30000);

// Single-quotes a string for a POSIX shell.
QString shellQuote(const QString& s);
QString humanSize(qint64 bytes);
QString cacheDir(const QString& sub);

} // namespace Tools
