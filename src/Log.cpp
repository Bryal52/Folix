#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStringList>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

constexpr qint64 kMaxSize = 2 * 1024 * 1024;

QRecursiveMutex g_mutex;   // a Qt warning raised while writing comes back here
int g_crashFd = -1;

const char* levelName(Log::Level level)
{
    switch (level) {
    case Log::Info: return "инфо   ";
    case Log::Warning: return "внимание";
    case Log::Error: return "ОШИБКА ";
    }
    return "";
}

void onQtMessage(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    std::fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    if (type == QtWarningMsg)
        Log::warning(QStringLiteral("Qt"), msg);
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        Log::error(QStringLiteral("Qt"), msg);
}

void writeRaw(const char* s)
{
    if (g_crashFd >= 0)
        ::write(g_crashFd, s, std::strlen(s));
}

// Only async-signal-safe calls in here.
void onCrash(int sig)
{
    writeRaw("\n*** Folix аварийно завершился: ");
    writeRaw(sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGABRT ? "SIGABRT"
                                                                   : sig == SIGFPE ? "SIGFPE" : "SIGILL");
    writeRaw("\nГде это случилось:\n");
    void* frames[64];
    const int n = backtrace(frames, 64);
    if (g_crashFd >= 0)
        backtrace_symbols_fd(frames, n, g_crashFd);
    writeRaw("***\n");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

Log& Log::instance()
{
    static Log* log = new Log;
    return *log;
}

QString Log::filePath()
{
    const QString dir = QDir::homePath() + QStringLiteral("/Library/Logs/Folix");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/folix.log");
}

void Log::install()
{
    qInstallMessageHandler(onQtMessage);
    g_crashFd = ::open(QFile::encodeName(filePath()).constData(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
        std::signal(sig, onCrash);
    info(QStringLiteral("Folix"), QStringLiteral("запуск"));
}

void Log::info(const QString& where, const QString& message) { instance().write(Info, where, message); }
void Log::warning(const QString& where, const QString& message) { instance().write(Warning, where, message); }
void Log::error(const QString& where, const QString& message) { instance().write(Error, where, message); }

void Log::write(Level level, const QString& where, const QString& message)
{
    {
        QMutexLocker lock(&g_mutex);
        const QString path = filePath();
        if (QFileInfo(path).size() > kMaxSize) {
            QFile::remove(path + QStringLiteral(".1"));
            QFile::rename(path, path + QStringLiteral(".1"));
            if (g_crashFd >= 0) {
                ::close(g_crashFd);
                g_crashFd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
            }
        }
        QFile f(path);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QString text = message.trimmed();
            text.replace(QLatin1Char('\n'), QStringLiteral("\n                                 "));
            const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                + QStringLiteral("  ") + QString::fromUtf8(levelName(level)) + QStringLiteral("  [") + where
                + QStringLiteral("] ") + text + QLatin1Char('\n');
            f.write(line.toUtf8());
        }
    }
    if (level == Error)
        ++m_unseen;
    emit added(level);
}

QString Log::read(int maxLines)
{
    QMutexLocker lock(&g_mutex);
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    if (lines.size() > maxLines)
        lines = lines.mid(lines.size() - maxLines);
    return lines.join(QLatin1Char('\n'));
}

void Log::clear()
{
    {
        QMutexLocker lock(&g_mutex);
        QFile::resize(filePath(), 0);
        QFile::remove(filePath() + QStringLiteral(".1"));
    }
    instance().markSeen();
}

void Log::markSeen()
{
    if (m_unseen.exchange(0) > 0)
        emit added(Info);
}
