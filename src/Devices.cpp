#include "Devices.h"

#include "AdbBackend.h"
#include "FileTypes.h"
#include "LocalBackend.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>

Devices& Devices::instance()
{
    static Devices d;
    return d;
}

Devices::Devices() : m_local(std::make_shared<LocalBackend>())
{
    loadProfiles();
}

QString Devices::kindName(DeviceKind kind)
{
    switch (kind) {
    case DeviceKind::Mac: return QStringLiteral("Mac");
    case DeviceKind::Android: return QStringLiteral("Android");
    case DeviceKind::Vita: return QStringLiteral("PS Vita");
    case DeviceKind::Switch: return QStringLiteral("Nintendo Switch");
    case DeviceKind::PS3: return QStringLiteral("PlayStation 3");
    case DeviceKind::Other: return QStringLiteral("FTP");
    case DeviceKind::Archive: return QStringLiteral("Архив");
    }
    return {};
}

QList<DeviceChoice> Devices::choices()
{
    QList<DeviceChoice> out;
    auto place = [&](const QString& label, QStandardPaths::StandardLocation loc, const QString& icon) {
        const QString path = QStandardPaths::writableLocation(loc);
        out.append({QStringLiteral("mac:") + path, label, placeIcon(icon)});
    };
    place(QStringLiteral("Домашняя папка"), QStandardPaths::HomeLocation, QStringLiteral("home"));
    place(QStringLiteral("Рабочий стол"), QStandardPaths::DesktopLocation, QStringLiteral("desktop"));
    place(QStringLiteral("Загрузки"), QStandardPaths::DownloadLocation, QStringLiteral("downloads"));
    place(QStringLiteral("Документы"), QStandardPaths::DocumentsLocation, QStringLiteral("documents"));
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes()) {
        const QString root = vol.rootPath();
        if (!vol.isReady())
            continue;
#ifdef Q_OS_WIN
        const QString letter = root.left(2);
        const QString label = vol.name().isEmpty() ? QStringLiteral("Диск %1").arg(letter)
                                                   : QStringLiteral("%1 (%2)").arg(vol.name(), letter);
        out.append({QStringLiteral("mac:") + root, label, placeIcon(QStringLiteral("volume"))});
#else
        if (!root.startsWith(QLatin1String("/Volumes/")))
            continue;
        out.append({QStringLiteral("mac:") + root, vol.displayName(), placeIcon(QStringLiteral("volume"))});
#endif
    }

    bool first = true;
    for (const auto& d : AdbBackend::devices()) {
        DeviceChoice c;
        c.key = QStringLiteral("adb:") + d.serial;
        c.icon = deviceIcon(DeviceKind::Android);
        c.separatorBefore = first;
        first = false;
        const QString name = d.model.isEmpty() ? d.serial : d.model;
        if (d.state == QLatin1String("device")) {
            c.label = name + QStringLiteral(" (USB)");
            m_adbModels.insert(d.serial, d.model);
        } else if (d.state == QLatin1String("unauthorized")) {
            c.label = name + QStringLiteral(" — разрешите отладку");
            c.usable = false;
            c.hint = QStringLiteral("На экране телефона появился запрос «Разрешить отладку по USB?». "
                                    "Нажмите «Разрешить», затем выберите телефон ещё раз.");
        } else {
            c.label = name + QStringLiteral(" — ") + d.state;
            c.usable = false;
            c.hint = QStringLiteral("Телефон не готов (%1). Переподключите кабель.").arg(d.state);
        }
        out.append(c);
    }

#ifdef Q_OS_WIN
    const QString programs = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files")));
#else
    const QString programs = QStringLiteral("/Applications");
#endif
    out.append({QStringLiteral("mac:") + programs, QStringLiteral("Программы"), placeIcon(QStringLiteral("apps")), true});

    first = true;
    for (const FtpProfile& p : std::as_const(m_profiles)) {
        out.append({QStringLiteral("ftp:") + p.id, p.name, deviceIcon(p.kind), first});
        first = false;
    }

    out.append({QStringLiteral("action:new"), QStringLiteral("Новое FTP-подключение…"),
                placeIcon(QStringLiteral("add")), true});
    return out;
}

BackendPtr Devices::backend(const QString& key)
{
    if (key.startsWith(QLatin1String("mac:")))
        return m_local;
    if (const auto it = m_remote.constFind(key); it != m_remote.constEnd())
        return *it;

    BackendPtr b;
    if (key.startsWith(QLatin1String("adb:"))) {
        const QString serial = key.mid(4);
        b = std::make_shared<AdbBackend>(serial, m_adbModels.value(serial));
    } else if (key.startsWith(QLatin1String("ftp:"))) {
        const FtpProfile p = profile(key.mid(4));
        if (p.id.isEmpty())
            return nullptr;
        b = std::make_shared<FtpBackend>(p);
    }
    if (b)
        m_remote.insert(key, b);
    return b;
}

QString Devices::startPath(const QString& key)
{
    if (key.startsWith(QLatin1String("mac:")))
        return key.mid(4);
    if (auto b = backend(key))
        return b->homePath();
    return QStringLiteral("/");
}

FtpProfile Devices::profile(const QString& id) const
{
    for (const FtpProfile& p : m_profiles) {
        if (p.id == id)
            return p;
    }
    return {};
}

QString Devices::addProfile(FtpProfile p)
{
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_profiles.append(p);
    saveProfiles();
    emit profilesChanged();
    return p.id;
}

void Devices::updateProfile(const FtpProfile& p)
{
    for (FtpProfile& existing : m_profiles) {
        if (existing.id == p.id)
            existing = p;
    }
    m_remote.remove(QStringLiteral("ftp:") + p.id);
    saveProfiles();
    emit profilesChanged();
}

void Devices::removeProfile(const QString& id)
{
    m_profiles.erase(std::remove_if(m_profiles.begin(), m_profiles.end(),
                                    [&](const FtpProfile& p) { return p.id == id; }),
                     m_profiles.end());
    m_remote.remove(QStringLiteral("ftp:") + id);
    saveProfiles();
    emit profilesChanged();
}

void Devices::loadProfiles()
{
    QSettings s;
    const int n = s.beginReadArray(QStringLiteral("ftp"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        FtpProfile p;
        p.id = s.value(QStringLiteral("id")).toString();
        p.name = s.value(QStringLiteral("name")).toString();
        p.kind = DeviceKind(s.value(QStringLiteral("kind"), int(DeviceKind::Other)).toInt());
        p.host = s.value(QStringLiteral("host")).toString();
        p.port = s.value(QStringLiteral("port"), 21).toInt();
        p.user = s.value(QStringLiteral("user")).toString();
        p.password = s.value(QStringLiteral("password")).toString();
        if (!p.id.isEmpty())
            m_profiles.append(p);
    }
    s.endArray();
}

void Devices::saveProfiles()
{
    QSettings s;
    s.beginWriteArray(QStringLiteral("ftp"), int(m_profiles.size()));
    for (int i = 0; i < m_profiles.size(); ++i) {
        const FtpProfile& p = m_profiles[i];
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), p.id);
        s.setValue(QStringLiteral("name"), p.name);
        s.setValue(QStringLiteral("kind"), int(p.kind));
        s.setValue(QStringLiteral("host"), p.host);
        s.setValue(QStringLiteral("port"), p.port);
        s.setValue(QStringLiteral("user"), p.user);
        s.setValue(QStringLiteral("password"), p.password);
    }
    s.endArray();
}
