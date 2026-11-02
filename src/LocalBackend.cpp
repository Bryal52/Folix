#include "LocalBackend.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUrl>

#include <QMutex>
#include <QWaitCondition>

#include <fcntl.h>
#include <sys/attr.h>
#include <sys/vnode.h>
#include <unistd.h>

#include <cstring>
#include <thread>
#include <vector>

namespace {

void copyRange(const QString& from, const QString& to, qint64 offset, qint64 length, bool append,
               const Progress& cb)
{
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly))
        throw FsError(QStringLiteral("Не удалось открыть %1: %2").arg(from, in.errorString()));
    QFile out(to);
    if (!out.open(append ? QIODevice::Append : (QIODevice::WriteOnly | QIODevice::Truncate)))
        throw FsError(QStringLiteral("Не удалось создать %1: %2").arg(to, out.errorString()));
    if (offset > 0)
        in.seek(offset);

    const qint64 total = length < 0 ? in.size() - offset : length;
    qint64 remaining = total;
    qint64 done = 0;
    QByteArray buf(4 << 20, Qt::Uninitialized);
    while (remaining > 0) {
        const qint64 n = in.read(buf.data(), qMin<qint64>(buf.size(), remaining));
        if (n <= 0)
            throw FsError(QStringLiteral("Ошибка чтения %1").arg(from));
        if (out.write(buf.constData(), n) != n)
            throw FsError(QStringLiteral("Ошибка записи %1: %2").arg(to, out.errorString()));
        done += n;
        remaining -= n;
        if (cb && !cb(done, total)) {
            out.close();
            if (!append)
                QFile::remove(to);
            throw Cancelled();
        }
    }
}

} // namespace

QString LocalBackend::homePath() const
{
    return QDir::homePath();
}

QList<Entry> LocalBackend::list(const QString& dir)
{
    const QFileInfo dirInfo(dir);
    if (!dirInfo.exists() || !dirInfo.isDir())
        throw FsError(QStringLiteral("Папка не найдена: %1").arg(dir));
    if (!dirInfo.isReadable())
        throw FsError(QStringLiteral("Нет доступа к папке %1").arg(dir));

    QList<Entry> result;
    const auto infos = QDir(dir).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::NoSort);
    result.reserve(infos.size());
    for (const QFileInfo& fi : infos) {
        Entry e;
        e.name = fi.fileName();
        e.isDir = fi.isDir();
        e.hidden = fi.isHidden();
        e.size = e.isDir ? -1 : fi.size();
        e.mtime = fi.lastModified();
        result.append(e);
    }
    return result;
}

void LocalBackend::mkdir(const QString& path)
{
    if (!QDir().mkpath(path))
        throw FsError(QStringLiteral("Не удалось создать папку %1").arg(path));
}

void LocalBackend::removeFile(const QString& path)
{
    if (!QFile::remove(path))
        throw FsError(QStringLiteral("Не удалось удалить %1").arg(path));
}

void LocalBackend::removeDir(const QString& path)
{
    if (!QDir().rmdir(path))
        throw FsError(QStringLiteral("Не удалось удалить папку %1").arg(path));
}

void LocalBackend::removeRecursive(const QString& path)
{
    const QFileInfo fi(path);
    const bool ok = (fi.isDir() && !fi.isSymLink()) ? QDir(path).removeRecursively() : QFile::remove(path);
    if (!ok)
        throw FsError(QStringLiteral("Не удалось удалить %1").arg(path));
}

void LocalBackend::moveToTrash(const QString& path)
{
    if (!QFile::moveToTrash(path))
        throw FsError(QStringLiteral("Не удалось переместить в Корзину: %1").arg(path));
}

void LocalBackend::rename(const QString& from, const QString& to)
{
    if (!QDir().rename(from, to))
        throw FsError(QStringLiteral("Не удалось переименовать %1").arg(from));
}

void LocalBackend::download(const QString& path, const QString& localFile, const Progress& cb, bool append)
{
    copyRange(path, localFile, 0, -1, append, cb);
}

void LocalBackend::upload(const QString& localFile, const QString& path, const Progress& cb,
                          qint64 offset, qint64 length)
{
    copyRange(localFile, path, offset, length, false, cb);
}

QByteArray LocalBackend::read(const QString& path, qint64 maxBytes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw FsError(QStringLiteral("Не удалось открыть %1").arg(path));
    return f.read(maxBytes);
}

qint64 LocalBackend::dirSize(const QString& path, const std::atomic_bool* cancel)
{
    qint64 total = 0;
    int n = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        if ((++n & 255) == 0 && cancel && *cancel)
            throw Cancelled();
    }
    return total;
}

QString LocalBackend::mediaUrl(const QString& path) const
{
    return QUrl::fromLocalFile(path).toString();
}

qint64 LocalBackend::freeSpace(const QString& path)
{
    return QStorageInfo(path).bytesAvailable();
}

bool LocalBackend::skipInScan(const QString& path) const
{
    // Other apps' sandboxes (macOS asks for permission), cloud placeholders (reading them
    // downloads listings) and the Trash (needs Full Disk Access).
    static const QStringList skipped = [] {
        const QString h = QDir::homePath();
        return QStringList{
            h + QStringLiteral("/Library/Containers"), h + QStringLiteral("/Library/Group Containers"),
            h + QStringLiteral("/Library/Daemon Containers"), h + QStringLiteral("/Library/Mobile Documents"),
            h + QStringLiteral("/Library/CloudStorage"), h + QStringLiteral("/.Trash"),
        };
    }();
    for (const QString& s : skipped) {
        if (path == s || path.startsWith(s + QLatin1Char('/')))
            return true;
    }
    return false;
}

SizeMap LocalBackend::scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress)
{
    // Several threads share a queue of folders. Each folder is read with getattrlistbulk, which
    // returns names, types and sizes of a whole batch of entries in one system call.
    struct Shared {
        QMutex mutex;
        QWaitCondition wake;
        QList<QByteArray> queue;
        int active = 0;
        SizeMap own;
        qint64 files = 0;
    } s;
    const QByteArray rootBytes = QFile::encodeName(root);
    s.own.insert(root, 0);
    s.queue << rootBytes;
    auto cancelled = [cancel] { return cancel && cancel->load(); };

    auto work = [&] {
        std::vector<char> buf(256 * 1024);
        attrlist al{};
        al.bitmapcount = ATTR_BIT_MAP_COUNT;
        al.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_ERROR | ATTR_CMN_OBJTYPE;
        al.fileattr = ATTR_FILE_DATALENGTH;

        for (;;) {
            QByteArray dir;
            {
                QMutexLocker lock(&s.mutex);
                while (s.queue.isEmpty() && s.active > 0 && !cancelled())
                    s.wake.wait(&s.mutex, QDeadlineTimer(100));
                if (cancelled() || s.queue.isEmpty()) {
                    s.wake.wakeAll();
                    return;
                }
                dir = s.queue.takeLast();
                ++s.active;
            }

            qint64 sum = 0;
            qint64 files = 0;
            QList<QByteArray> subdirs;
            const int fd = ::open(dir.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (fd >= 0) {
                int count;
                while ((count = getattrlistbulk(fd, &al, buf.data(), buf.size(), 0)) > 0) {
                    const char* entry = buf.data();
                    for (int i = 0; i < count; ++i) {
                        const char* field = entry;
                        uint32_t length;
                        std::memcpy(&length, field, sizeof(length));
                        field += sizeof(uint32_t);
                        attribute_set_t returned;
                        std::memcpy(&returned, field, sizeof(returned));
                        field += sizeof(attribute_set_t);
                        uint32_t error = 0;
                        if (returned.commonattr & ATTR_CMN_ERROR) {
                            std::memcpy(&error, field, sizeof(error));
                            field += sizeof(uint32_t);
                        }
                        const char* name = nullptr;
                        if (returned.commonattr & ATTR_CMN_NAME) {
                            attrreference_t ref;
                            std::memcpy(&ref, field, sizeof(ref));
                            name = field + ref.attr_dataoffset;
                            field += sizeof(attrreference_t);
                        }
                        fsobj_type_t type = VNON;
                        if (returned.commonattr & ATTR_CMN_OBJTYPE) {
                            std::memcpy(&type, field, sizeof(type));
                            field += sizeof(fsobj_type_t);
                        }
                        if (!error && name) {
                            if (type == VREG) {
                                if (returned.fileattr & ATTR_FILE_DATALENGTH) {
                                    off_t size;
                                    std::memcpy(&size, field, sizeof(size));
                                    sum += size;
                                }
                                ++files;
                            } else if (type == VDIR) {
                                QByteArray child = dir;
                                if (!child.endsWith('/'))
                                    child += '/';
                                child += name;
                                subdirs << child;
                            }
                        }
                        entry += length;
                    }
                }
                ::close(fd);
            }

            QStringList childKeys;
            QList<QByteArray> accepted;
            for (const QByteArray& c : std::as_const(subdirs)) {
                const QString key = QFile::decodeName(c);   // NFC, like the rest of Qt
                if (skipInScan(key))
                    continue;
                childKeys << key;
                accepted << c;
            }
            const QString dirKey = dir == rootBytes ? root : QFile::decodeName(dir);
            qint64 filesNow = 0;
            {
                QMutexLocker lock(&s.mutex);
                s.own[dirKey] += sum;
                for (const QString& k : std::as_const(childKeys))
                    s.own.insert(k, 0);
                s.queue += accepted;
                s.files += files;
                filesNow = s.files;
                --s.active;
                s.wake.wakeAll();
            }
            if (progress && (filesNow & ~0xFFF) != ((filesNow - files) & ~0xFFF))
                progress(filesNow);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back(work);
    for (std::thread& t : threads)
        t.join();
    if (cancelled())
        throw Cancelled();
    if (progress)
        progress(s.files);
    return aggregateSizes(root, s.own);
}
