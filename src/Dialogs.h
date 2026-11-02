#pragma once

#include <QString>
#include <QStringList>

class QWidget;

// Native-looking macOS sheets with Russian button titles.
namespace Dialogs {

bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& okText,
             bool destructive = false);
// Returns the index of the pressed button, or -1 for "Отмена".
int choose(QWidget* parent, const QString& title, const QString& text, const QStringList& buttons);
void error(QWidget* parent, const QString& title, const QString& text, const QStringList& details = {});
void info(QWidget* parent, const QString& title, const QString& text);
QString askText(QWidget* parent, const QString& title, const QString& label, const QString& initial,
                const QString& okText, bool* ok);

} // namespace Dialogs
