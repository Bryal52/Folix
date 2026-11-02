#include "SizeIndex.h"

#include "Log.h"
#include "SecureStore.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QStorageInfo>
#include <QThreadPool>
#include <QtConcurrent>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace {

constexpr quint32 kFormatVersion = 2;
constexpr int kMaxFsRefreshes = 300;

QString normalize(const QString& path)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

bool isUnder(const QString& path, const QString& dir)
{
    if (dir == QLatin1String("/"))
        return path.size() > 1 && path.startsWith(QLatin1Char('/'));
    return path.startsWith(dir) && path.size() > dir.size() && path[dir.size()] == QLatin1Char('/');
}

bool within(const QString& path, const QString& root)
{
    return path == root || isUnder(path, root);
}

// "/a/b/c/d" under "/a/b" -> "/a/b/c"
QString childOf(const QString& path, const QString& dir)
{
    const qsizetype start = dir == QLatin1String("/") ? 1 : dir.size() + 1;
    const qsizetype slash = path.indexOf(QLatin1Char('/'), start);
    return slash < 0 ? path : path.left(slash);
}

QString cacheName(const QString& key)
{
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex())
        + QStringLiteral(".bin");
}

struct Snapshot {
    bool ok = false;
    QDateTime at;
    SizeMap sizes;
};

Snapshot loadCache(const QString& key)
{
    Snapshot s;
    const QByteArray packed = SecureStore::load(cacheName(key));
    if (packed.isEmpty())
        return s;
    const QByteArray data = qUncompress(packed);
    if (data.isEmpty())
        return s;
    QDataStream in(data);
    in.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0;
    QString storedKey;
    qint64 atMs = 0;
    in >> version >> storedKey >> atMs >> s.sizes;
    if (in.status() != QDataStream::Ok || version != kFormatVersion || storedKey != key)
        return s;
    s.at = QDateTime::fromMSecsSinceEpoch(atMs);
    if (s.at.secsTo(QDateTime::currentDateTime()) >= SizeIndex::kTtlSecs) {
        SecureStore::remove(cacheName(key));
        return s;
    }
    s.ok = true;
    return s;
}

void writeCache(const QString& key, const QDateTime& at, const SizeMap& sizes)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_6_0);
    out << kFormatVersion << key << at.toMSecsSinceEpoch() << sizes;
    SecureStore::save(cacheName(key), qCompress(data, 6));
}

// Without decrypting: a cache file not written for a day is certainly older than a day.
void purgeExpired()
{
    const QDir dir(SecureStore::directory());
    const QDateTime limit = QDateTime::currentDateTime().addSecs(-SizeIndex::kTtlSecs);
    for (const QFileInfo& fi : dir.entryInfoList({QStringLiteral("*.bin")}, QDir::Files)) {
        if (fi.lastModified() < limit)
            QFile::remove(fi.absoluteFilePath());
    }
}

struct ScanResult {
    bool ok = false;
    QString error;
    SizeMap sizes;
};

struct RefreshResult {
    bool ok = false;
    QString dir;
    qint64 size = 0;
    QSet<QString> present;   // child folders that still exist
    SizeMap rescanned;       // subtrees scanned again
    QStringList stale;       // old keys under vanished or rescanned children
};

void fsCallback(ConstFSEventStreamRef, void*, size_t count, void* paths, const FSEventStreamEventFlags*,
                const FSEventStreamEventId*)
{
    auto** list = static_cast<char**>(paths);
    QStringList dirs;
    dirs.reserve(qsizetype(count));
    for (size_t i = 0; i < count; ++i)
        dirs << normalize(QFile::decodeName(list[i]));
    // Runs on a private queue; hand the folders over to the GUI thread.
    QMetaObject::invokeMethod(&SizeIndex::instance(), [dirs] { SizeIndex::instance().onFsEvents(dirs); },
                              Qt::QueuedConnection);
}

} // namespace

struct SizeIndex::Device {
    QString key;
    QString backendId;
    QString root;
    BackendPtr backend;
    SizeMap sizes;
    QDateTime scannedAt;
    QString error;
    bool scanning = false;
    bool diskTried = false;
    bool sessionScanned = false;
    bool refreshing = false;
    bool saveQueued = false;
    QList<Job> pending;
    FSEventStreamRef stream = nullptr;
    std::shared_ptr<std::atomic<qint64>> files = std::make_shared<std::atomic<qint64>>(0);
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
};

SizeIndex& SizeIndex::instance()
{
    static SizeIndex* index = new SizeIndex;   // lives until exit; scans are cancelled in shutdown()
    return *index;
}

SizeIndex::SizeIndex()
{
    m_pool = new QThreadPool;   // never deleted: quitting must not wait for a scan
    m_pool->setMaxThreadCount(4);
    m_pool->start(purgeExpired);

    m_progressTimer.setInterval(400);
    connect(&m_progressTimer, &QTimer::timeout, this, [this] {
        bool any = false;
        for (const auto& d : std::as_const(m_devices)) {
            if (d->scanning) {
                any = true;
                emit progress(d->backendId);
            }
        }
        if (!any)
            m_progressTimer.stop();
    });

    m_fsTimer.setSingleShot(true);
    m_fsTimer.setInterval(1500);
    connect(&m_fsTimer, &QTimer::timeout, this, &SizeIndex::flushFsEvents);
}

QString SizeIndex::localRootFor(const QString& path)
{
    const QString home = QDir::homePath();
    if (within(path, home))
        return home;
    if (within(path, QStringLiteral("/Applications")))
        return QStringLiteral("/Applications");
    if (!path.startsWith(QLatin1String("/Volumes/")))
        return {};
    const QString root = QStringLiteral("/Volumes/") + path.section(QLatin1Char('/'), 2, 2);
    if (QFileInfo(root).isSymLink())
        return {};   // "Macintosh HD" link to the system disk
    static const QByteArrayList network = {"smbfs", "afpfs", "nfs", "webdav", "ftp", "cifs"};
    if (network.contains(QStorageInfo(root).fileSystemType()))
        return {};
    return root;
}

std::shared_ptr<SizeIndex::Device> SizeIndex::device(const BackendPtr& b, const QString& root)
{
    const QString key = b->id() + QLatin1Char('|') + root;
    auto& d = m_devices[key];
    if (!d) {
        d = std::make_shared<Device>();
        d->key = key;
        d->backendId = b->id();
        d->root = root;
    }
    d->backend = b;
    return d;
}

std::shared_ptr<SizeIndex::Device> SizeIndex::find(const QString& backendId, const QString& path) const
{
    const QString p = normalize(path);
    std::shared_ptr<Device> best;
    for (const auto& d : m_devices) {
        if (d->backendId == backendId && within(p, d->root) && (!best || d->root.size() > best->root.size()))
            best = d;
    }
    return best;
}

bool SizeIndex::fresh(const Device& d)
{
    return d.scannedAt.isValid() && d.scannedAt.secsTo(QDateTime::currentDateTime()) < kTtlSecs;
}

void SizeIndex::ensure(const BackendPtr& b, const QString& root, bool force)
{
    if (!b)
        return;
    const auto d = device(b, normalize(root.isEmpty() ? b->homePath() : root));
    if (d->scanning)
        return;
    const bool local = b->isLocal();

    if (!force && !d->diskTried) {
        // First show what an earlier session saved.
        d->diskTried = true;
        d->scanning = true;
        const QString key = d->key;
        auto* watcher = new QFutureWatcher<Snapshot>(this);
        connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, watcher, d, local] {
            const Snapshot s = watcher->result();
            watcher->deleteLater();
            d->scanning = false;
            if (s.ok) {
                d->sizes = s.sizes;
                d->scannedAt = s.at;
                emit changed(d->backendId);
            }
            if (local ? !d->sessionScanned : !fresh(*d))
                startScan(d);
        });
        watcher->setFuture(QtConcurrent::run(m_pool, [key] { return loadCache(key); }));
        return;
    }
    if (!force && (local ? d->sessionScanned : fresh(*d)))
        return;
    startScan(d);
}

void SizeIndex::startScan(const std::shared_ptr<Device>& d)
{
    d->scanning = true;
    d->sessionScanned = true;
    d->error.clear();
    d->pending.clear();   // the full scan sees those changes anyway
    d->cancel = std::make_shared<std::atomic_bool>(false);
    *d->files = 0;
    const auto cancel = d->cancel;
    const auto files = d->files;
    const BackendPtr b = d->backend;
    const QString root = d->root;
    if (!m_progressTimer.isActive())
        m_progressTimer.start();
    emit changed(d->backendId);

    auto* watcher = new QFutureWatcher<ScanResult>(this);
    connect(watcher, &QFutureWatcher<ScanResult>::finished, this, [this, watcher, d] {
        const ScanResult r = watcher->result();
        watcher->deleteLater();
        d->scanning = false;
        if (r.ok) {
            d->sizes = r.sizes;
            d->scannedAt = QDateTime::currentDateTime();
            save(d);
            if (d->backend->isLocal())
                watch(d);
        } else {
            d->error = r.error;
            if (!r.error.isEmpty() && r.error != Cancelled().message())
                Log::warning(QStringLiteral("Размеры папок"), d->root + QStringLiteral(": ") + r.error);
        }
        emit changed(d->backendId);
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [b, root, cancel, files] {
        ScanResult r;
        try {
            r.sizes = b->scanTree(root, cancel.get(), [files](qint64 n) {
                qint64 cur = files->load();
                while (n > cur && !files->compare_exchange_weak(cur, n)) {
                }
            });
            r.ok = true;
        } catch (const FsError& e) {
            r.error = e.message();
        }
        return r;
    }));
}

void SizeIndex::refresh(const BackendPtr& b, const QString& dir, const QStringList& changedNames)
{
    if (!b)
        return;
    const QString path = normalize(dir);
    const auto d = find(b->id(), path);
    if (!d || d->scanning || d->sizes.isEmpty() || b->skipInScan(path))
        return;
    for (const Job& j : std::as_const(d->pending)) {
        if (j.dir == path && j.changed == changedNames)
            return;   // already queued
    }
    d->pending.append({b, path, changedNames});
    runNextRefresh(d);
}

void SizeIndex::runNextRefresh(const std::shared_ptr<Device>& d)
{
    // One refresh at a time per index keeps parent totals consistent.
    if (d->refreshing || d->scanning || d->pending.isEmpty())
        return;
    d->refreshing = true;
    const Job job = d->pending.takeFirst();
    const SizeMap snapshot = d->sizes;   // implicitly shared, no real copy
    const auto cancel = d->cancel;

    auto* watcher = new QFutureWatcher<RefreshResult>(this);
    connect(watcher, &QFutureWatcher<RefreshResult>::finished, this, [this, watcher, d] {
        const RefreshResult r = watcher->result();
        watcher->deleteLater();
        d->refreshing = false;
        if (r.ok && !d->scanning) {
            const qint64 old = d->sizes.value(r.dir, -1);
            const bool touched = old != r.size || !r.rescanned.isEmpty() || !r.stale.isEmpty();
            for (const QString& key : r.stale)
                d->sizes.remove(key);
            d->sizes.insert(r.rescanned);
            d->sizes.insert(r.dir, r.size);
            if (old >= 0 && old != r.size) {
                const qint64 delta = r.size - old;
                QString p = r.dir;
                while (p != d->root && p != QLatin1String("/")) {
                    p = parentPath(p);
                    const auto it = d->sizes.find(p);
                    if (it != d->sizes.end())
                        *it += delta;
                }
            }
            if (touched) {
                saveSoon(d);
                emit changed(d->backendId);
            }
        }
        runNextRefresh(d);
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [job, snapshot, cancel] {
        RefreshResult r;
        try {
            qint64 total = 0;
            for (const Entry& e : job.backend->list(job.dir)) {
                if (e.isLink)
                    continue;
                if (!e.isDir) {
                    total += qMax<qint64>(0, e.size);
                    continue;
                }
                const QString path = joinPath(job.dir, e.name);
                if (job.backend->skipInScan(path))
                    continue;
                r.present.insert(path);
                if (job.changed.contains(e.name) || !snapshot.contains(path)) {
                    const SizeMap sub = job.backend->scanTree(path, cancel.get(), nullptr);
                    total += sub.value(path);
                    r.rescanned.insert(sub);
                } else {
                    total += snapshot.value(path);
                }
            }
            for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
                if (!isUnder(it.key(), job.dir))
                    continue;
                const QString child = childOf(it.key(), job.dir);
                if (!r.present.contains(child) || r.rescanned.contains(child))
                    r.stale << it.key();
            }
            r.dir = job.dir;
            r.size = total;
            r.ok = true;
        } catch (const FsError&) {
        }
        return r;
    }));
}

void SizeIndex::rename(const QString& backendId, const QString& from, const QString& to)
{
    const QString a = normalize(from), b = normalize(to);
    for (const auto& d : std::as_const(m_devices)) {
        if (d->backendId != backendId || d->sizes.isEmpty())
            continue;
        SizeMap moved;
        for (auto it = d->sizes.begin(); it != d->sizes.end();) {
            if (within(it.key(), a)) {
                moved.insert(b + it.key().mid(a.size()), it.value());
                it = d->sizes.erase(it);
            } else {
                ++it;
            }
        }
        d->sizes.insert(moved);
    }
}

void SizeIndex::save(const std::shared_ptr<Device>& d)
{
    const QString key = d->key;
    const QDateTime at = d->scannedAt;
    const SizeMap sizes = d->sizes;
    m_pool->start([key, at, sizes] { writeCache(key, at, sizes); });
}

void SizeIndex::saveSoon(const std::shared_ptr<Device>& d)
{
    // The Mac changes all the time; write its index at most once a minute.
    if (d->saveQueued)
        return;
    d->saveQueued = true;
    QTimer::singleShot(d->backend->isLocal() ? 60000 : 3000, this, [this, d] {
        d->saveQueued = false;
        save(d);
    });
}

void SizeIndex::watch(const std::shared_ptr<Device>& d)
{
    if (d->stream)
        return;
    CFStringRef root = d->root.toCFString();
    CFArrayRef paths = CFArrayCreate(nullptr, reinterpret_cast<const void**>(&root), 1, &kCFTypeArrayCallBacks);
    FSEventStreamContext context{};
    d->stream = FSEventStreamCreate(nullptr, fsCallback, &context, paths, kFSEventStreamEventIdSinceNow, 2.0,
                                    kFSEventStreamCreateFlagNone);
    CFRelease(paths);
    CFRelease(root);
    if (!d->stream)
        return;
    static dispatch_queue_t queue = dispatch_queue_create("folix.fsevents", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(d->stream, queue);
    FSEventStreamStart(d->stream);
}

void SizeIndex::onFsEvents(const QStringList& dirs)
{
    for (const QString& dir : dirs)
        m_fsPending.insert(dir);
    if (!m_fsTimer.isActive())
        m_fsTimer.start();
}

void SizeIndex::flushFsEvents()
{
    QStringList dirs = m_fsPending.values();
    m_fsPending.clear();
    // Deepest folders first, so parents are refreshed with their children's new totals.
    std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) {
        return a.count(QLatin1Char('/')) > b.count(QLatin1Char('/'));
    });
    if (dirs.size() > kMaxFsRefreshes)
        dirs = dirs.mid(0, kMaxFsRefreshes);
    for (const QString& dir : std::as_const(dirs)) {
        const auto d = find(QStringLiteral("local"), dir);
        if (d && d->stream)
            refresh(d->backend, dir);
    }
}

qint64 SizeIndex::size(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d ? d->sizes.value(normalize(path), -1) : -1;
}

bool SizeIndex::has(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d && !d->sizes.isEmpty();
}

bool SizeIndex::scanning(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d && d->scanning;
}

qint64 SizeIndex::scannedFiles(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d ? d->files->load() : 0;
}

QDateTime SizeIndex::scannedAt(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d ? d->scannedAt : QDateTime();
}

QString SizeIndex::lastError(const QString& backendId, const QString& path) const
{
    const auto d = find(backendId, path);
    return d ? d->error : QString();
}

void SizeIndex::shutdown()
{
    for (const auto& d : std::as_const(m_devices)) {
        *d->cancel = true;
        if (d->stream) {
            FSEventStreamStop(d->stream);
            FSEventStreamInvalidate(d->stream);
            FSEventStreamRelease(d->stream);
            d->stream = nullptr;
        }
        if (!d->sizes.isEmpty() && d->saveQueued)
            writeCache(d->key, d->scannedAt, d->sizes);   // keep the latest Mac sizes
    }
}
