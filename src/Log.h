#pragma once

#include <QObject>
#include <QString>

#include <atomic>

// Error log in ~/Library/Logs/Folix/folix.log (also visible in Console.app).
class Log : public QObject {
    Q_OBJECT
public:
    enum Level { Info, Warning, Error };

    static Log& instance();
    // Opens the file and catches Qt warnings and crashes. Call once at start-up.
    static void install();

    static void info(const QString& where, const QString& message);
    static void warning(const QString& where, const QString& message);
    static void error(const QString& where, const QString& message);

    static QString filePath();
    static QString read(int maxLines = 5000);
    static void clear();

    int unseenErrors() const { return m_unseen; }
    void markSeen();

signals:
    void added(int level);

private:
    void write(Level level, const QString& where, const QString& message);

    std::atomic<int> m_unseen{0};
};
