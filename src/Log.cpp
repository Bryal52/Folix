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

#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr qint64 kMaxSize = 2 * 1024 * 1024;

QRecursiveMutex g_mutex;   // a Qt warning raised while writing comes back here
#ifdef Q_OS_WIN
HANDLE g_crashFile = INVALID_HANDLE_VALUE;
#else
int g_crashFd = -1;
#endif

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

#ifdef Q_OS_WIN
void writeRaw(const char* s)
{
    DWORD written = 0;
    if (g_crashFile != INVALID_HANDLE_VALUE)
        WriteFile(g_crashFile, s, DWORD(std::strlen(s)), &written, nullptr);
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info)
{
    char line[160];
    std::snprintf(line, sizeof(line), "\n*** Folix аварийно завершился: исключение 0x%08lX по адресу %p\n***\n",
                  info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    writeRaw(line);
    return EXCEPTION_CONTINUE_SEARCH;
}

void openCrashFile()
{
    if (g_crashFile != INVALID_HANDLE_VALUE)
        CloseHandle(g_crashFile);
    g_crashFile = CreateFileW(reinterpret_cast<const wchar_t*>(Log::filePath().utf16()), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
}
#else
void writeRaw(const char* s)
{
    if (g_crashFd >= 0)
        ::write(g_crashFd, s, std::strlen(s));
}

void openCrashFile()
{
    if (g_crashFd >= 0)
        ::close(g_crashFd);
    g_crashFd = ::open(QFile::encodeName(Log::filePath()).constData(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
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
#endif

} // namespace

Log& Log::instance()
{
    static Log* log = new Log;
    return *log;
}

QString Log::filePath()
{
#ifdef Q_OS_WIN
    const QString dir = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + QStringLiteral("/Folix/Logs");
#else
    const QString dir = QDir::homePath() + QStringLiteral("/Library/Logs/Folix");
#endif
    QDir().mkpath(dir);
    return dir + QStringLiteral("/folix.log");
}

void Log::install()
{
    qInstallMessageHandler(onQtMessage);
    openCrashFile();
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(onCrash);
#else
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
        std::signal(sig, onCrash);
#endif
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
            openCrashFile();
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
