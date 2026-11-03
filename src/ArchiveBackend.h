#pragma once

#include "Backend.h"

#include "../third_party/Packix/core/Types.h"

#include <QMutex>
#include <QHash>
#include <QSet>

// An archive shown in a panel like a folder (used when the Packix app is not installed).
// Paths inside are "/folder/file"; the Packix engine (7-Zip) does the work.
class ArchiveBackend : public Backend {
public:
    ArchiveBackend(const QString& archivePath, const packix::ArchiveInfo& info, const QString& password);

    QString id() const override { return QStringLiteral("archive:") + m_path; }
    QString title() const override;
    DeviceKind kind() const override { return DeviceKind::Archive; }
    QString homePath() const override { return QStringLiteral("/"); }
    QString archivePath() const { return m_path; }
    bool canModify() const;

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
    SizeMap scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress) override;
    qint64 freeSpace(const QString&) override { return -1; }

    // Whole selections at once (one 7-Zip run instead of one per file).
    // `replace`: names that already exist at the destination and may be overwritten.
    void extractTo(const QStringList& paths, const QString& localDir, const QSet<QString>& replace,
                   const Progress& cb);
    void addFrom(const QStringList& localPaths, const QString& dir, const Progress& cb);

private:
    void reload();
    void requireModify() const;
    static QString inner(const QString& path);   // "/a/b" -> "a/b"
    // Names as 7-Zip stored them (on a Mac often with "ё", "й" decomposed); Folix shows them composed.
    QString raw(const QString& path) const;
    void buildNames();
    packix::Status run(const std::function<packix::Status(packix::Progress*)>& work, qint64 total,
                       const Progress& cb);
    void check(const packix::Status& st) const;

    QString m_path;
    QString m_password;
    mutable QMutex m_mutex;
    packix::ArchiveInfo m_info;
    QHash<QString, QString> m_raw;   // composed inner path -> stored inner path
};
