#pragma once

#include "Backend.h"

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QTimer>

class QThreadPool;

// Folder sizes. One index per scanned root: a connected device, the Mac home folder or an
// external drive. Indexes are kept encrypted on disk for 24 hours and patched after changes:
// Folix's own operations everywhere, plus file system events on the Mac.
class SizeIndex : public QObject {
    Q_OBJECT
public:
    static SizeIndex& instance();
    static constexpr qint64 kTtlSecs = 24 * 3600;

    // Devices: cached data is reused for a day. Mac roots: cached data is shown at once and
    // rescanned once per session, then kept current through file system events.
    void ensure(const BackendPtr& b, const QString& root = {}, bool force = false);
    // Re-reads `dir` after a change; `changed` folders inside it are scanned again.
    void refresh(const BackendPtr& b, const QString& dir, const QStringList& changed = {});
    // Moves known sizes along with a renamed or moved folder.
    void rename(const QString& backendId, const QString& from, const QString& to);

    qint64 size(const QString& backendId, const QString& path) const;   // -1 if unknown
    bool has(const QString& backendId, const QString& path) const;
    bool scanning(const QString& backendId, const QString& path) const;
    qint64 scannedFiles(const QString& backendId, const QString& path) const;
    QDateTime scannedAt(const QString& backendId, const QString& path) const;
    QString lastError(const QString& backendId, const QString& path) const;

    // The Mac root a local path belongs to: the home folder or an external drive.
    static QString localRootFor(const QString& path);
    void shutdown();
    // Called by the file system watcher with folders whose contents changed.
    void onFsEvents(const QStringList& dirs);

signals:
    void changed(const QString& backendId);    // sizes changed
    void progress(const QString& backendId);   // scan progress only

private:
    struct Job {
        BackendPtr backend;
        QString dir;
        QStringList changed;
    };
    struct Device;

    SizeIndex();
    std::shared_ptr<Device> device(const BackendPtr& b, const QString& root);
    std::shared_ptr<Device> find(const QString& backendId, const QString& path) const;
    void startScan(const std::shared_ptr<Device>& d);
    void runNextRefresh(const std::shared_ptr<Device>& d);
    void save(const std::shared_ptr<Device>& d);
    void saveSoon(const std::shared_ptr<Device>& d);
    void watch(const std::shared_ptr<Device>& d);
    void flushFsEvents();
    static bool fresh(const Device& d);

    QHash<QString, std::shared_ptr<Device>> m_devices;   // key: backendId|root
    QThreadPool* m_pool;
    QTimer m_progressTimer;
    QTimer m_fsTimer;
    QSet<QString> m_fsPending;
};
