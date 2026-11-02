#pragma once

#include <QString>

// Opening Folix when a drive is connected: a LaunchAgent runs FolixAgent from the app bundle.
namespace AutoOpen {

QString supportDir();    // ~/Library/Application Support/Folix
QString socketPath();    // where a running Folix listens for "open <path>"

bool isEnabled();
void setEnabled(bool on);
// First launch from /Applications turns it on; later launches keep the agent pointing at this app.
void syncOnStartup();

QString volumeUuid(const QString& mountPoint);
void ignoreVolume(const QString& uuid);
void clearIgnored();
int ignoredCount();

} // namespace AutoOpen
