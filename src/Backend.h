#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>

enum class DeviceKind { Mac, Android, Vita, Switch, PS3, Other };

struct Entry {
    QString name;
    bool isDir = false;
    bool hidden = false;
    bool isLink = false;
    qint64 size = -1;           // folders: -1 until counted, -2 while counting
    QDateTime mtime;
    QStringList parts;          // PS3 split file (name.66600, name.66601, ...)
    QList<qint64> partSizes;
};

class FsError : public std::runtime_error {
public:
    explicit FsError(const QString& msg) : std::runtime_error(msg.toStdString()) {}
    QString message() const { return QString::fromStdString(what()); }
};

class Cancelled : public FsError {
public:
    Cancelled() : FsError(QStringLiteral("Отменено")) {}
};

// Called with bytes done and total (-1 if unknown). Return false to cancel.
using Progress = std::function<bool(qint64 done, qint64 total)>;

// Folder path -> total size of everything inside it (recursively).
using SizeMap = QHash<QString, qint64>;
using ScanProgress = std::function<void(qint64 files)>;

QString joinPath(const QString& dir, const QString& name);
QString parentPath(const QString& path);
QString baseName(const QString& path);

// Collapses PS3 split files (name.66600, name.66601, ...) into one entry.
void groupSplitParts(QList<Entry>& entries);

// Real names on the device behind an entry (all parts for a split file).
QStringList realNames(const Entry& e);

// Turns per-folder sizes of direct files into recursive folder sizes.
SizeMap aggregateSizes(const QString& root, const SizeMap& own);

// All calls are blocking and may be made from worker threads. Errors throw FsError.
class Backend {
public:
    virtual ~Backend() = default;

    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual DeviceKind kind() const = 0;
    virtual bool isLocal() const { return false; }
    virtual QString homePath() const = 0;

    virtual QList<Entry> list(const QString& dir) = 0;
    virtual void mkdir(const QString& path) = 0;
    virtual void removeFile(const QString& path) = 0;
    virtual void removeDir(const QString& path) = 0;
    virtual void removeRecursive(const QString& path);
    virtual void rename(const QString& from, const QString& to) = 0;

    // Copies a file from the device to the Mac.
    virtual void download(const QString& path, const QString& localFile, const Progress& cb,
                          bool append = false) = 0;
    // Copies a file (or a byte range of it) from the Mac to the device.
    virtual void upload(const QString& localFile, const QString& path, const Progress& cb,
                        qint64 offset = 0, qint64 length = -1) = 0;
    virtual QByteArray read(const QString& path, qint64 maxBytes) = 0;

    // Fast copy inside the same device; returns false if not supported.
    virtual bool copyWithin(const QString&, const QString&) { return false; }
    virtual qint64 dirSize(const QString& path, const std::atomic_bool* cancel);
    // Sizes of every folder under root (root included).
    virtual SizeMap scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress);
    // Folders a full scan should not enter (system partitions, virtual mounts).
    virtual bool skipInScan(const QString&) const { return false; }
    // URL that ffmpeg / the video player can open directly, or empty.
    virtual QString mediaUrl(const QString&) const { return {}; }
    virtual qint64 freeSpace(const QString&) { return -1; }
};

using BackendPtr = std::shared_ptr<Backend>;
