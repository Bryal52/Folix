// Packix's log inside Folix: replaces third_party/Packix/dialogs/Log.cpp so that everything the
// archive windows report goes into Folix's own log (~/Library/Logs/Folix) and its "Логи" window.

#include "../third_party/Packix/dialogs/Log.h"
#include "Log.h"

namespace packix::ui {

Log& Log::instance()
{
    static Log* log = [] {
        auto* l = new Log;
        // Folix's log button and Packix's windows both learn about new entries.
        QObject::connect(&::Log::instance(), &::Log::added, l, &Log::added);
        return l;
    }();
    return *log;
}

void Log::install()
{
    // Folix has already installed its own handler at start-up.
}

void Log::info(const QString& where, const QString& message) { ::Log::info(where, message); }
void Log::warning(const QString& where, const QString& message) { ::Log::warning(where, message); }
void Log::error(const QString& where, const QString& message) { ::Log::error(where, message); }

QString Log::filePath() { return ::Log::filePath(); }
QString Log::read(int maxLines) { return ::Log::read(maxLines); }
void Log::clear() { ::Log::clear(); }

void Log::markSeen()
{
    m_unseen = 0;
    ::Log::instance().markSeen();
}

void Log::write(Level level, const QString& where, const QString& message)
{
    switch (level) {
    case Info: ::Log::info(where, message); break;
    case Warning: ::Log::warning(where, message); break;
    case Error: ::Log::error(where, message); break;
    }
}

} // namespace packix::ui
