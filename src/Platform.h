#pragma once

#include <QString>

// The few things that differ between macOS and Windows in the interface.
namespace Platform {

QString localName();          // "Mac" / "Этот компьютер"
QString fileManagerName();    // "Finder" / "Проводник"
void reveal(const QString& path);
// Shortcut hints are written Mac-style (⌘, ⇧, ⌥, ⌫); on Windows they become Ctrl+, Shift+, ...
QString keys(QString text);

} // namespace Platform
