#pragma once

#include "Backend.h"
#include "FtpBackend.h"

#include <QHash>
#include <QIcon>
#include <QObject>

class LocalBackend;

// A row in a panel's device menu.
struct DeviceChoice {
    QString key;          // "mac:<path>", "adb:<serial>", "ftp:<id>", "action:new"
    QString label;
    QIcon icon;
    bool separatorBefore = false;
    bool usable = true;
    QString hint;         // shown when an unusable device is picked
};

class Devices : public QObject {
    Q_OBJECT
public:
    static Devices& instance();

    QList<DeviceChoice> choices();
    BackendPtr backend(const QString& key);
    // Backends that are not devices (an open archive) are registered by whoever opens them.
    void registerBackend(const QString& key, const BackendPtr& b) { m_remote.insert(key, b); }
    QString startPath(const QString& key);
    std::shared_ptr<LocalBackend> local() const { return m_local; }

    const QList<FtpProfile>& profiles() const { return m_profiles; }
    FtpProfile profile(const QString& id) const;
    QString addProfile(FtpProfile p);
    void updateProfile(const FtpProfile& p);
    void removeProfile(const QString& id);

    static QString kindName(DeviceKind kind);

signals:
    void profilesChanged();

private:
    Devices();
    void loadProfiles();
    void saveProfiles();

    std::shared_ptr<LocalBackend> m_local;
    QHash<QString, BackendPtr> m_remote;
    QHash<QString, QString> m_adbModels;
    QList<FtpProfile> m_profiles;
};
