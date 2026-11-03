#include "ArchiveBackend.h"

#include "../third_party/Packix/core/Archiver.h"
#include "../third_party/Packix/core/Temp.h"
#include "../third_party/Packix/dialogs/Dialogs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <thread>

ArchiveBackend::ArchiveBackend(const QString& archivePath, const packix::ArchiveInfo& info, const QString& password)
    : m_path(archivePath), m_password(password), m_info(info)
{
    buildNames();
}

void ArchiveBackend::buildNames()
{
    m_raw.clear();
    for (const packix::Item& item : std::as_const(m_info.items)) {
        QString rawPrefix, nicePrefix;
        for (const QString& part : item.path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            rawPrefix += (rawPrefix.isEmpty() ? QString() : QStringLiteral("/")) + part;
            nicePrefix += (nicePrefix.isEmpty() ? QString() : QStringLiteral("/")) + part.normalized(QString::NormalizationForm_C);
            m_raw.insert(nicePrefix, rawPrefix);
        }
    }
}

QString ArchiveBackend::raw(const QString& path) const
{
    const QString nice = inner(path).normalized(QString::NormalizationForm_C);
    QMutexLocker lock(&m_mutex);
    return m_raw.value(nice, nice);
}

QString ArchiveBackend::title() const
{
    return QFileInfo(m_path).fileName();
}

bool ArchiveBackend::canModify() const
{
    QMutexLocker lock(&m_mutex);
    return packix::Archiver::canModify(m_info);
}

QString ArchiveBackend::inner(const QString& path)
{
    QString p = path;
    while (p.startsWith(QLatin1Char('/')))
        p.remove(0, 1);
    while (p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p;
}

void ArchiveBackend::check(const packix::Status& st) const
{
    if (st.ok())
        return;
    if (st.error == packix::Error::Cancelled)
        throw Cancelled();
    throw FsError(packix::ui::Dialogs::errorText(st, QFileInfo(m_path).fileName()));
}

void ArchiveBackend::requireModify() const
{
    if (!canModify())
        throw FsError(QStringLiteral("Архив «%1» нельзя изменять: этот формат только для чтения.")
                          .arg(QFileInfo(m_path).fileName()));
}

void ArchiveBackend::reload()
{
    packix::ArchiveInfo fresh;
    check(packix::Archiver::open(m_path, m_password, fresh));
    QMutexLocker lock(&m_mutex);
    m_info = fresh;
    buildNames();
}

// Runs a Packix operation on its own thread and turns its percent into Folix progress.
packix::Status ArchiveBackend::run(const std::function<packix::Status(packix::Progress*)>& work, qint64 total,
                                   const Progress& cb)
{
    packix::Progress progress;
    packix::Status result;
    std::atomic_bool finished{false};
    std::thread worker([&] {
        result = work(&progress);
        finished = true;
    });
    while (!finished) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        const int percent = progress.snapshot().percent;
        if (cb && !cb(percent > 0 ? total * percent / 100 : 0, total))
            progress.cancel = true;
    }
    worker.join();
    if (cb && result.ok())
        cb(total, total);
    return result;
}

QList<Entry> ArchiveBackend::list(const QString& dir)
{
    const QString base = raw(dir);
    const QString prefix = base.isEmpty() ? QString() : base + QLatin1Char('/');
    QHash<QString, Entry> found;
    QStringList order;

    QMutexLocker lock(&m_mutex);
    for (const packix::Item& item : std::as_const(m_info.items)) {
        if (!item.path.startsWith(prefix) || item.path.size() == prefix.size())
            continue;
        const QString rest = item.path.mid(prefix.size());
        const qsizetype slash = rest.indexOf(QLatin1Char('/'));
        const QString name = (slash < 0 ? rest : rest.left(slash)).normalized(QString::NormalizationForm_C);
        auto it = found.find(name);
        if (it == found.end()) {
            Entry e;
            e.name = name;
            e.isDir = slash >= 0 || item.isDir;
            e.size = 0;
            e.hidden = name.startsWith(QLatin1Char('.'));
            it = found.insert(name, e);
            order << name;
        }
        if (slash < 0) {
            it->isDir = item.isDir;
            it->mtime = item.modified;
            if (!item.isDir)
                it->size = item.size;
        } else if (!item.isDir) {
            it->size += item.size;   // folder sizes come straight from the archive listing
        }
    }
    QList<Entry> out;
    out.reserve(order.size());
    for (const QString& n : std::as_const(order))
        out << found.value(n);
    return out;
}

SizeMap ArchiveBackend::scanTree(const QString& root, const std::atomic_bool*, const ScanProgress& progress)
{
    // Everything is in the listing already: no reading from the archive.
    const QString base = raw(root);
    const QString prefix = base.isEmpty() ? QString() : base + QLatin1Char('/');
    const QString rootKey = base.isEmpty() ? QStringLiteral("/") : QLatin1Char('/') + inner(root).normalized(QString::NormalizationForm_C);
    SizeMap sizes;
    sizes.insert(rootKey, 0);
    qint64 files = 0;

    QMutexLocker lock(&m_mutex);
    for (const packix::Item& item : std::as_const(m_info.items)) {
        if (!item.path.startsWith(prefix) || item.path.size() == prefix.size())
            continue;
        const QStringList parts = item.path.mid(prefix.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        const int folders = item.isDir ? int(parts.size()) : int(parts.size()) - 1;
        const qint64 size = item.isDir ? 0 : item.size;
        if (!item.isDir)
            ++files;
        sizes[rootKey] += size;
        QString key = base.isEmpty() ? QString() : rootKey;
        for (int i = 0; i < folders; ++i) {
            key += QLatin1Char('/') + parts[i].normalized(QString::NormalizationForm_C);
            sizes[key] += size;
        }
    }
    if (progress)
        progress(files);
    return sizes;
}

qint64 ArchiveBackend::dirSize(const QString& path, const std::atomic_bool* cancel)
{
    return scanTree(path, cancel, nullptr).value(path);
}

void ArchiveBackend::extractTo(const QStringList& paths, const QString& localDir, const QSet<QString>& replace,
                               const Progress& cb)
{
    packix::ArchiveInfo info;
    {
        QMutexLocker lock(&m_mutex);
        info = m_info;
    }
    QStringList items;
    qint64 total = 0;
    for (const QString& p : paths) {
        const QString in = raw(p);
        items << in;
        for (const packix::Item& item : std::as_const(info.items)) {
            if (!item.isDir && (item.path == in || item.path.startsWith(in + QLatin1Char('/'))))
                total += item.size;
        }
    }

    packix::Archiver::ExtractRequest req;
    req.archive = m_path;
    req.destination = localDir;
    req.items = items;
    req.password = m_password;
    // Folix has already asked about names that exist; those are replaced, nothing else is touched.
    req.onConflict = [localDir, replace](const QString& existing, bool) {
        const QString top = QDir(localDir).relativeFilePath(existing).section(QLatin1Char('/'), 0, 0);
        return replace.contains(top) ? packix::Conflict::Replace : packix::Conflict::KeepBoth;
    };
    check(run([&](packix::Progress* pr) { return packix::Archiver::extract(req, pr); }, total, cb));
}

void ArchiveBackend::addFrom(const QStringList& localPaths, const QString& dir, const Progress& cb)
{
    requireModify();
    packix::ArchiveInfo info;
    {
        QMutexLocker lock(&m_mutex);
        info = m_info;
    }
    const qint64 total = packix::Archiver::diskSize(localPaths);
    const QString insideDir = raw(dir);
    check(run([&](packix::Progress* pr) { return packix::Archiver::add(info, localPaths, insideDir, m_password, pr); },
              total, cb));
    reload();
}

void ArchiveBackend::download(const QString& path, const QString& localFile, const Progress& cb, bool append)
{
    const QString tmp = packix::Temp::newDir(QStringLiteral("folix"));
    extractTo({path}, tmp, {}, cb);
    const QString extracted = tmp + QLatin1Char('/') + baseName(path);
    if (append) {
        QFile in(extracted), out(localFile);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::Append))
            throw FsError(QStringLiteral("Не удалось записать %1").arg(localFile));
        out.write(in.readAll());
    } else {
        QFile::remove(localFile);
        if (!QFile::rename(extracted, localFile) && !QFile::copy(extracted, localFile))
            throw FsError(QStringLiteral("Не удалось записать %1").arg(localFile));
    }
    QDir(tmp).removeRecursively();
}

void ArchiveBackend::upload(const QString& localFile, const QString& path, const Progress& cb, qint64 offset,
                            qint64 length)
{
    if (offset != 0 || length >= 0)
        throw FsError(QStringLiteral("Частичная запись в архив не поддерживается"));
    // The file goes in under its own name, so give it the right one first.
    QString source = localFile;
    QString tmp;
    if (QFileInfo(localFile).fileName() != baseName(path)) {
        tmp = packix::Temp::newDir(QStringLiteral("folix"));
        source = tmp + QLatin1Char('/') + baseName(path);
        if (!QFile::copy(localFile, source))
            throw FsError(QStringLiteral("Не удалось подготовить %1").arg(localFile));
    }
    addFrom({source}, parentPath(path), cb);
    if (!tmp.isEmpty())
        QDir(tmp).removeRecursively();
}

QByteArray ArchiveBackend::read(const QString& path, qint64 maxBytes)
{
    const QString tmp = packix::Temp::newDir(QStringLiteral("folix"));
    extractTo({path}, tmp, {}, nullptr);
    QFile f(tmp + QLatin1Char('/') + baseName(path));
    QByteArray data;
    if (f.open(QIODevice::ReadOnly))
        data = f.read(maxBytes);
    f.close();
    QDir(tmp).removeRecursively();
    if (data.isEmpty())
        throw FsError(QStringLiteral("Не удалось прочитать %1").arg(path));
    return data;
}

void ArchiveBackend::mkdir(const QString& path)
{
    requireModify();
    const QString tmp = packix::Temp::newDir(QStringLiteral("folix"));
    const QString folder = tmp + QLatin1Char('/') + baseName(path);
    QDir().mkpath(folder);
    addFrom({folder}, parentPath(path), nullptr);
    QDir(tmp).removeRecursively();
}

void ArchiveBackend::removeFile(const QString& path)
{
    removeRecursive(path);
}

void ArchiveBackend::removeDir(const QString& path)
{
    removeRecursive(path);
}

void ArchiveBackend::removeRecursive(const QString& path)
{
    requireModify();
    packix::ArchiveInfo info;
    {
        QMutexLocker lock(&m_mutex);
        info = m_info;
    }
    check(packix::Archiver::remove(info, {raw(path)}, m_password));
    reload();
}

void ArchiveBackend::rename(const QString&, const QString&)
{
    throw FsError(QStringLiteral("Внутри архива переименовывать нельзя. Распакуйте файл, переименуйте и добавьте обратно."));
}
