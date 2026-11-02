#pragma once

class QWidget;

// One permission instead of many: with Full Disk Access macOS stops asking separately
// for Desktop, Documents, Downloads, removable and network volumes.
namespace FullDiskAccess {

bool granted();          // checked silently, never triggers a macOS prompt
void openSettings();
// Explains and waits for the permission. Without `force` it is skipped when access is
// already granted or the user chose "don't ask again".
void ask(QWidget* parent, bool force = false);

} // namespace FullDiskAccess
