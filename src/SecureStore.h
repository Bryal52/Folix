#pragma once

#include <QByteArray>
#include <QString>

// Small encrypted files in ~/Library/Application Support/Folix.
// AES-256-CBC + HMAC-SHA256; the key is generated once and kept in the macOS Keychain.
namespace SecureStore {

QString directory();
bool save(const QString& name, const QByteArray& plain);
QByteArray load(const QString& name);   // empty if missing, damaged or the key is unavailable
void remove(const QString& name);

} // namespace SecureStore
