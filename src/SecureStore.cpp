#include "SecureStore.h"

#include <QDir>
#include <QFile>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <Security/Security.h>

#ifndef FOLIX_KEYCHAIN_SERVICE
#define FOLIX_KEYCHAIN_SERVICE "Folix"
#endif

namespace {

const QByteArray kMagic("FLX1");
constexpr int kKeySize = 64;   // 32 bytes AES key + 32 bytes HMAC key
constexpr int kIvSize = kCCBlockSizeAES128;
constexpr int kMacSize = CC_SHA256_DIGEST_LENGTH;

QMutex g_keyMutex;
bool g_keyTried = false;
QByteArray g_key;

CFDictionaryRef makeDict(const void** keys, const void** values, CFIndex n)
{
    return CFDictionaryCreate(nullptr, keys, values, n, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
}

QByteArray key()
{
    QMutexLocker lock(&g_keyMutex);
    if (g_keyTried)
        return g_key;
    g_keyTried = true;

    const void* qk[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
    const void* qv[] = {kSecClassGenericPassword, CFSTR(FOLIX_KEYCHAIN_SERVICE), CFSTR("size-cache-key"), kCFBooleanTrue,
                        kSecMatchLimitOne};
    CFDictionaryRef query = makeDict(qk, qv, 5);
    CFTypeRef result = nullptr;
    OSStatus status = SecItemCopyMatching(query, &result);
    CFRelease(query);

    if (status == errSecSuccess && result) {
        auto data = static_cast<CFDataRef>(result);
        g_key = QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), CFDataGetLength(data));
        CFRelease(result);
        if (g_key.size() != kKeySize)
            g_key.clear();
        return g_key;
    }
    if (status != errSecItemNotFound)
        return {};   // access denied: work without the cache

    QByteArray fresh(kKeySize, Qt::Uninitialized);
    if (SecRandomCopyBytes(kSecRandomDefault, kKeySize, fresh.data()) != errSecSuccess)
        return {};
    CFDataRef value = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(fresh.constData()), fresh.size());
    const void* ak[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecAttrLabel, kSecValueData};
    const void* av[] = {kSecClassGenericPassword, CFSTR(FOLIX_KEYCHAIN_SERVICE), CFSTR("size-cache-key"),
                        CFSTR("Folix size cache key"), value};
    CFDictionaryRef attrs = makeDict(ak, av, 5);
    status = SecItemAdd(attrs, nullptr);
    CFRelease(attrs);
    CFRelease(value);
    if (status == errSecSuccess)
        g_key = fresh;
    return g_key;
}

QByteArray hmac(const QByteArray& k, const QByteArray& data)
{
    QByteArray mac(kMacSize, Qt::Uninitialized);
    CCHmac(kCCHmacAlgSHA256, k.constData() + 32, 32, data.constData(), size_t(data.size()), mac.data());
    return mac;
}

bool equalConstantTime(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

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
    const QByteArray k = key();
    if (k.isEmpty())
        return false;

    QByteArray iv(kIvSize, Qt::Uninitialized);
    if (SecRandomCopyBytes(kSecRandomDefault, kIvSize, iv.data()) != errSecSuccess)
        return false;
    QByteArray cipher(plain.size() + kCCBlockSizeAES128, Qt::Uninitialized);
    size_t written = 0;
    if (CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, k.constData(), kCCKeySizeAES256, iv.constData(),
                plain.constData(), size_t(plain.size()), cipher.data(), size_t(cipher.size()), &written) != kCCSuccess)
        return false;
    cipher.resize(qsizetype(written));

    const QByteArray body = kMagic + iv + cipher;
    QSaveFile f(pathFor(name));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(body);
    f.write(hmac(k, body));
    if (!f.commit())
        return false;
    QFile::setPermissions(pathFor(name), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QByteArray load(const QString& name)
{
    QFile f(pathFor(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    f.close();
    if (all.size() < kMagic.size() + kIvSize + kMacSize || !all.startsWith(kMagic))
        return {};

    const QByteArray k = key();
    if (k.isEmpty())
        return {};
    const QByteArray body = all.left(all.size() - kMacSize);
    if (!equalConstantTime(hmac(k, body), all.right(kMacSize)))
        return {};   // damaged or written with another key

    const QByteArray iv = body.mid(kMagic.size(), kIvSize);
    const QByteArray cipher = body.mid(kMagic.size() + kIvSize);
    QByteArray plain(cipher.size() + kCCBlockSizeAES128, Qt::Uninitialized);
    size_t written = 0;
    if (CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, k.constData(), kCCKeySizeAES256, iv.constData(),
                cipher.constData(), size_t(cipher.size()), plain.data(), size_t(plain.size()), &written) != kCCSuccess)
        return {};
    plain.resize(qsizetype(written));
    return plain;
}

void remove(const QString& name)
{
    QFile::remove(pathFor(name));
}

} // namespace SecureStore
