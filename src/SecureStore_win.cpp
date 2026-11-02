#include "SecureStore.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dpapi.h>

// Windows: data is encrypted with DPAPI, so only this Windows user can read it back.

namespace {

const QByteArray kMagic("FLW1");

QString pathFor(const QString& name)
{
    return SecureStore::directory() + QLatin1Char('/') + name;
}

} // namespace

namespace SecureStore {

QString directory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cache");
    QDir().mkpath(dir);
    return dir;
}

bool save(const QString& name, const QByteArray& plain)
{
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Folix", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    const QByteArray cipher(reinterpret_cast<const char*>(out.pbData), qsizetype(out.cbData));
    LocalFree(out.pbData);

    QSaveFile f(pathFor(name));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(kMagic + cipher);
    return f.commit();
}

QByteArray load(const QString& name)
{
    QFile f(pathFor(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    if (!all.startsWith(kMagic))
        return {};
    const QByteArray cipher = all.mid(kMagic.size());
    DATA_BLOB in{DWORD(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};   // damaged or from another user
    const QByteArray plain(reinterpret_cast<const char*>(out.pbData), qsizetype(out.cbData));
    LocalFree(out.pbData);
    return plain;
}

void remove(const QString& name)
{
    QFile::remove(pathFor(name));
}

} // namespace SecureStore
