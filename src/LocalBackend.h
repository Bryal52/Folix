#pragma once

#include "Backend.h"

class LocalBackend : public Backend {
public:
    QString id() const override { return QStringLiteral("local"); }
    QString title() const override;
    DeviceKind kind() const override { return DeviceKind::Mac; }
    bool isLocal() const override { return true; }
    QString homePath() const override;

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
    qint64 dirSize(const QString& path, const std::atomic_bool* cancel) override;
    QString mediaUrl(const QString& path) const override;
    qint64 freeSpace(const QString& path) override;
    SizeMap scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress) override;
    bool skipInScan(const QString& path) const override;

    void moveToTrash(const QString& path);
};
