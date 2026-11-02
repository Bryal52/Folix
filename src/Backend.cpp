#include "Backend.h"

#include <QHash>
#include <QRegularExpression>

QString joinPath(const QString& dir, const QString& name)
{
    if (dir.isEmpty())
        return name;
    if (dir.endsWith(QLatin1Char('/')))
        return dir + name;
    return dir + QLatin1Char('/') + name;
}

QString parentPath(const QString& path)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    const int i = p.lastIndexOf(QLatin1Char('/'));
    if (i <= 0)
        return QStringLiteral("/");
    return p.left(i);
}

QString baseName(const QString& path)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p.mid(p.lastIndexOf(QLatin1Char('/')) + 1);
}

void groupSplitParts(QList<Entry>& entries)
{
    static const QRegularExpression re(QStringLiteral("^(.+)\\.666\\d\\d$"));
    QHash<QString, int> groups;
    QList<Entry> out;
    out.reserve(entries.size());
    for (const Entry& e : entries) {
        const auto m = re.match(e.name);
        if (e.isDir || !m.hasMatch()) {
            out.append(e);
            continue;
        }
        const QString base = m.captured(1);
        auto it = groups.find(base);
        if (it == groups.end()) {
            Entry g;
            g.name = base;
            g.size = 0;
            g.mtime = e.mtime;
            g.hidden = e.hidden;
            it = groups.insert(base, out.size());
            out.append(g);
        }
        Entry& g = out[*it];
        g.parts.append(e.name);
        g.partSizes.append(qMax<qint64>(0, e.size));
        g.size += qMax<qint64>(0, e.size);
    }
    for (Entry& e : out) {
        if (e.parts.size() < 2)
            continue;
        // keep sizes aligned with sorted part names
        QList<QPair<QString, qint64>> zipped;
        for (int i = 0; i < e.parts.size(); ++i)
            zipped.append({e.parts[i], e.partSizes[i]});
        std::sort(zipped.begin(), zipped.end());
        for (int i = 0; i < zipped.size(); ++i) {
            e.parts[i] = zipped[i].first;
            e.partSizes[i] = zipped[i].second;
        }
    }
    entries = out;
}

QStringList realNames(const Entry& e)
{
    return e.parts.isEmpty() ? QStringList{e.name} : e.parts;
}

void Backend::removeRecursive(const QString& path)
{
    for (const Entry& e : list(path)) {
        const QString child = joinPath(path, e.name);
        if (e.isDir)
            removeRecursive(child);
        else
            removeFile(child);
    }
    removeDir(path);
}

SizeMap aggregateSizes(const QString& root, const SizeMap& own)
{
    auto depth = [](const QString& p) { return p == QLatin1String("/") ? 0 : int(p.count(QLatin1Char('/'))); };
    QStringList dirs = own.keys();
    std::sort(dirs.begin(), dirs.end(), [&](const QString& a, const QString& b) { return depth(a) > depth(b); });
    SizeMap total = own;
    for (const QString& d : std::as_const(dirs)) {
        if (d == root)
            continue;
        const auto it = total.find(parentPath(d));
        if (it != total.end())
            *it += total.value(d);
    }
    return total;
}

SizeMap Backend::scanTree(const QString& root, const std::atomic_bool* cancel, const ScanProgress& progress)
{
    SizeMap own;
    own.insert(root, 0);
    QStringList queue{root};
    qint64 files = 0;
    while (!queue.isEmpty()) {
        if (cancel && *cancel)
            throw Cancelled();
        const QString dir = queue.takeLast();
        QList<Entry> entries;
        try {
            entries = list(dir);
        } catch (const Cancelled&) {
            throw;
        } catch (const FsError&) {
            if (dir == root)
                throw;
            continue;
        }
        for (const Entry& e : std::as_const(entries)) {
            if (e.isLink)
                continue;
            const QString path = joinPath(dir, e.name);
            if (e.isDir) {
                if (skipInScan(path))
                    continue;
                own.insert(path, 0);
                queue.append(path);
            } else {
                own[dir] += qMax<qint64>(0, e.size);
                ++files;
            }
        }
        if (progress)
            progress(files);
    }
    return aggregateSizes(root, own);
}

qint64 Backend::dirSize(const QString& path, const std::atomic_bool* cancel)
{
    qint64 total = 0;
    for (const Entry& e : list(path)) {
        if (cancel && *cancel)
            throw Cancelled();
        if (e.isDir)
            total += dirSize(joinPath(path, e.name), cancel);
        else if (e.size > 0)
            total += e.size;
    }
    return total;
}
