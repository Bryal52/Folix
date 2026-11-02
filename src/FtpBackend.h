#pragma once

#include "Backend.h"

#include <QMutex>

typedef void CURL;

struct FtpProfile {
    QString id;          // stable id for settings
    QString name;
    DeviceKind kind = DeviceKind::Other;
    QString host;
    int port = 21;
    QString user;
    QString password;
};

class FtpBackend : public Backend {
public:
    explicit FtpBackend(const FtpProfile& profile);
    ~FtpBackend() override;

    QString id() const override { return QStringLiteral("ftp:") + m_p.id; }
    QString title() const override { return m_p.name; }
    DeviceKind kind() const override { return m_p.kind; }
    QString homePath() const override { return QStringLiteral("/"); }
    const FtpProfile& profile() const { return m_p; }

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
    QString mediaUrl(const QString& path) const override;
    SizeMap scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress) override;
    bool skipInScan(const QString& path) const override;

    static QList<Entry> parseListing(const QByteArray& data);

private:
    friend class CurlHandle;
    CURL* acquire();
    void release(CURL* h);
    QByteArray urlFor(const QString& path, bool isDir) const;
    void quote(const QStringList& commands);

    FtpProfile m_p;
    QMutex m_mutex;
    QList<CURL*> m_pool;   // idle handles keep their connections open
};
