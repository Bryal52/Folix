#pragma once

#include <QString>
#include <QStringList>

// Archives in Folix. When the Packix app is installed it handles them (Folix hands files over);
// otherwise Folix uses the Packix engine built into it.
namespace Archives {

// Start-up: Packix windows inside Folix speak Russian like the rest of Folix; old temp files go.
void init();
void shutdown();

// Settings (Настройки): archives can be switched off entirely, and handing them to Packix too.
bool enabled();
void setEnabled(bool on);
bool preferPackixApp();
void setPreferPackixApp(bool on);

// An archive Folix should react to (when archive support is on).
bool isArchive(const QString& fileName);
// Archives go to the Packix app: it is installed and the setting allows it.
bool useApp();

// The installed Packix app, or empty.
QString packixApp();
bool packixInstalled();
void openInPackix(const QStringList& archives);
// False when the running Packix could not be reached; then Folix shows the window itself.
bool archiveInPackix(const QStringList& files);

} // namespace Archives
