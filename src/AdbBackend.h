#pragma once

#include "Backend.h"
#include "Tools.h"

class AdbBackend : public Backend {
public:
    struct Device {
        QString serial;
        QString model;
        QString state;   // "device", "unauthorized", "offline", ...
    };
    static QList<Device> devices();

    AdbBackend(const QString& serial, const QString& model);

    QString id() const override { return QStringLiteral("adb:") + m_serial; }
    QString title() const override { return m_model.isEmpty() ? m_serial : m_model; }
    DeviceKind kind() const override { return DeviceKind::Android; }
    QString homePath() const override { return QStringLiteral("/storage/emulated/0"); }
    QString serial() const { return m_serial; }

    QList<Entry> list(const QString& dir) override;
    void mkdir(const QString& path) override;
    void removeFile(const QString& path) override;
    void removeDir(const QString& path) override;
    void removeRecursive(const QString& path) override;
    void rename(const QString& from, const QString& to) override;
    void download(const QString& path, const QString& localFile, const Progress& cb,
                  bool append = false) override;
    void upload(const QString& localFile, const QString& path, const Progress& cb,
                qint64 offset = 0, qint64 length = -1) override;
    QByteArray read(const QString& path, qint64 maxBytes) override;
    bool copyWithin(const QString& from, const QString& to) override;
    qint64 dirSize(const QString& path, const std::atomic_bool* cancel) override;
    qint64 freeSpace(const QString& path) override;
    SizeMap scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress) override;

private:
    Tools::Result shell(const QString& cmd, int timeoutMs = 30000) const;
    void shellChecked(const QString& cmd, int timeoutMs = 120000) const;
    qint64 remoteSize(const QString& path) const;

    QString m_serial;
    QString m_model;
};
