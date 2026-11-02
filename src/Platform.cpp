#include "Platform.h"

#include <QDir>
#include <QProcess>

namespace Platform {

QString localName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Этот компьютер");
#else
    return QStringLiteral("Mac");
#endif
}

QString fileManagerName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("Проводник");
#else
    return QStringLiteral("Finder");
#endif
}

void reveal(const QString& path)
{
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
#else
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
#endif
}

QString keys(QString text)
{
#ifdef Q_OS_WIN
    text.replace(QStringLiteral("⌘⌫"), QStringLiteral("Ctrl+Backspace"));
    text.replace(QStringLiteral("⌫"), QStringLiteral("Backspace"));
    text.replace(QStringLiteral("⌘↑"), QStringLiteral("Ctrl+↑"));
    text.replace(QStringLiteral("⇧⌘"), QStringLiteral("Ctrl+Shift+"));
    text.replace(QStringLiteral("⌥⌘"), QStringLiteral("Ctrl+Alt+"));
    text.replace(QStringLiteral("⌘"), QStringLiteral("Ctrl+"));
    text.replace(QStringLiteral("⇧"), QStringLiteral("Shift+"));
    text.replace(QStringLiteral("⌥"), QStringLiteral("Alt+"));
#endif
    return text;
}

} // namespace Platform
