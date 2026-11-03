#include "Operations.h"

#include "ArchiveBackend.h"
#include "JobDialog.h"
#include "LocalBackend.h"
#include "Tools.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace {

// FAT32 cannot hold files of 4 GiB or more; webMAN understands name.66600, name.66601, ...
constexpr qint64 kFat32Max = 0xFFFFFFFFLL;
constexpr qint64 kSplitPart = 0xFFFF0000LL;

struct PlanItem {
    QString src;            // device path (or first part for merged files)
    QString dst;
    bool isDir = false;
    qint64 size = 0;
    QStringList mergeParts; // split file merged into one file on the Mac
    QList<qint64> mergeSizes;
};

struct TopLevel {
    Entry entry;
    QList<PlanItem> plan;
};

QString partName(const QString& base, int i)
{
    return base + QStringLiteral(".666%1").arg(i, 2, 10, QLatin1Char('0'));
}

void planEntry(JobState& st, Backend& src, bool dstLocal, const QString& srcPath, const QString& dstPath,
               const Entry& e, QList<PlanItem>& plan)
{
    if (st.cancel)
        throw Cancelled();
    if (e.isDir) {
        plan.append({srcPath, dstPath, true, 0, {}, {}});
        st.setCurrent(QStringLiteral("Подсчёт: ") + srcPath);
        QList<Entry> kids = src.list(srcPath);
        groupSplitParts(kids);
        for (const Entry& k : std::as_const(kids))
            planEntry(st, src, dstLocal, joinPath(srcPath, k.name), joinPath(dstPath, k.name), k, plan);
        return;
    }
    if (e.parts.isEmpty()) {
        plan.append({srcPath, dstPath, false, qMax<qint64>(0, e.size), {}, {}});
        return;
    }
    const QString srcParent = parentPath(srcPath);
    const QString dstParent = parentPath(dstPath);
    if (dstLocal) {
        PlanItem m{srcPath, dstPath, false, e.size, {}, e.partSizes};
        for (const QString& p : e.parts)
            m.mergeParts.append(joinPath(srcParent, p));
        plan.append(m);
    } else {
        for (int i = 0; i < e.parts.size(); ++i)
            plan.append({joinPath(srcParent, e.parts[i]), joinPath(dstParent, e.parts[i]), false,
                         e.partSizes.value(i), {}, {}});
    }
}

QString tempFile()
{
    return Tools::cacheDir(QStringLiteral("transfer")) + QLatin1Char('/')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void copyFile(JobState& st, Backend& src, Backend& dst, const PlanItem& it, qint64 base, qint64 total, int factor)
{
    auto progress = [&](qint64 offset) {
        return [&st, base, total, offset](qint64 done, qint64) {
            st.setProgress(base + offset + done, total);
            return !st.cancel.load();
        };
    };

    if (!it.mergeParts.isEmpty()) {
        // PS3 split file -> one file on the Mac
        qint64 offset = 0;
        for (int i = 0; i < it.mergeParts.size(); ++i) {
            src.download(it.mergeParts[i], it.dst, progress(offset), i > 0);
            offset += it.mergeSizes.value(i);
        }
        return;
    }

    if (src.isLocal() && dst.isLocal()) {
        src.download(it.src, it.dst, progress(0));
    } else if (src.isLocal()) {
        const bool split = dst.kind() == DeviceKind::PS3 && it.size > kFat32Max
            && it.dst.startsWith(QLatin1String("/dev_usb"));
        if (!split) {
            dst.upload(it.src, it.dst, progress(0));
            return;
        }
        int i = 0;
        for (qint64 offset = 0; offset < it.size; offset += kSplitPart, ++i)
            dst.upload(it.src, partName(it.dst, i), progress(offset), offset, qMin(kSplitPart, it.size - offset));
    } else if (dst.isLocal()) {
        src.download(it.src, it.dst, progress(0));
    } else {
        // device -> device through a temporary file on the Mac
        const QString tmp = tempFile();
        try {
            src.download(it.src, tmp, progress(0));
            dst.upload(tmp, it.dst, progress(it.size * (factor - 1)));
        } catch (...) {
            QFile::remove(tmp);
            throw;
        }
        QFile::remove(tmp);
    }
}

void removeEntry(Backend& b, const QString& dir, const Entry& e)
{
    for (const QString& n : realNames(e)) {
        const QString path = joinPath(dir, n);
        if (e.isDir)
            b.removeRecursive(path);
        else
            b.removeFile(path);
    }
}

} // namespace

void runTransfer(JobState& st, const TransferRequest& r)
{
    Backend& src = *r.src;
    Backend& dst = *r.dst;
    const bool sameDevice = src.id() == dst.id();
    QList<Entry> items = r.items;

    // Same device: move is a rename, and Android can copy by itself.
    if (sameDevice && (r.move || dst.kind() == DeviceKind::Android)) {
        QList<Entry> rest;
        for (const Entry& e : std::as_const(items)) {
            if (st.cancel)
                throw Cancelled();
            if (r.overwrite.contains(e.name)) {
                rest.append(e);   // merging into an existing item needs a real copy
                continue;
            }
            st.setCurrent(e.name);
            try {
                for (const QString& n : realNames(e)) {
                    const QString from = joinPath(r.srcDir, n);
                    const QString to = joinPath(r.dstDir, n);
                    if (r.move)
                        src.rename(from, to);
                    else
                        src.copyWithin(from, to);
                }
            } catch (const Cancelled&) {
                throw;
            } catch (const FsError& err) {
                if (r.move && src.isLocal())
                    rest.append(e);   // different disk: fall back to copy + delete
                else
                    st.addError(e.name + QStringLiteral(": ") + err.message());
            }
        }
        items = rest;
        if (items.isEmpty())
            return;
    }

    // Archives: the whole selection in one 7-Zip run instead of one per file.
    auto progress = [&st](qint64 done, qint64 total) {
        st.setProgress(done, total);
        return !st.cancel.load();
    };
    if (auto* archive = dynamic_cast<ArchiveBackend*>(r.src.get()); archive && dst.isLocal()) {
        QStringList paths;
        for (const Entry& e : std::as_const(items))
            paths << joinPath(r.srcDir, e.name);
        st.setCurrent(QStringLiteral("Распаковка из «%1»").arg(archive->title()));
        archive->extractTo(paths, r.dstDir, r.overwrite, progress);
        if (r.move) {
            st.setCurrent(QStringLiteral("Удаление из архива"));
            for (const QString& p : std::as_const(paths))
                archive->removeRecursive(p);
        }
        return;
    }
    if (auto* archive = dynamic_cast<ArchiveBackend*>(r.dst.get()); archive && src.isLocal()) {
        QStringList paths;
        for (const Entry& e : std::as_const(items)) {
            for (const QString& n : realNames(e))
                paths << joinPath(r.srcDir, n);
        }
        st.setCurrent(QStringLiteral("Добавление в «%1»").arg(archive->title()));
        archive->addFrom(paths, r.dstDir, progress);
        if (r.move) {
            for (const QString& p : std::as_const(paths))
                src.removeRecursive(p);
        }
        return;
    }

    // Plan everything first so the progress bar knows the total.
    const bool dstLocal = dst.isLocal();
    const int factor = (!src.isLocal() && !dstLocal) ? 2 : 1;
    QList<TopLevel> tops;
    qint64 total = 0;
    for (const Entry& e : std::as_const(items)) {
        TopLevel t{e, {}};
        try {
            planEntry(st, src, dstLocal, joinPath(r.srcDir, e.name), joinPath(r.dstDir, e.name), e, t.plan);
        } catch (const Cancelled&) {
            throw;
        } catch (const FsError& err) {
            st.addError(e.name + QStringLiteral(": ") + err.message());
            continue;
        }
        for (const PlanItem& p : std::as_const(t.plan))
            total += p.size * factor;
        tops.append(t);
    }

    qint64 base = 0;
    for (const TopLevel& t : std::as_const(tops)) {
        bool failed = false;
        for (const PlanItem& it : t.plan) {
            if (st.cancel)
                throw Cancelled();
            if (it.isDir) {
                try {
                    dst.mkdir(it.dst);
                } catch (const FsError&) {
                    // usually "already exists"; real problems show up on the files inside
                }
                continue;
            }
            st.setCurrent(it.src);
            try {
                copyFile(st, src, dst, it, base, total, factor);
            } catch (const Cancelled&) {
                throw;
            } catch (const FsError& err) {
                st.addError(it.src + QStringLiteral(": ") + err.message());
                failed = true;
            }
            base += it.size * factor;
            st.setProgress(base, total);
        }
        if (r.move && !failed) {
            st.setCurrent(QStringLiteral("Удаление оригинала: ") + t.entry.name);
            try {
                removeEntry(src, r.srcDir, t.entry);
            } catch (const FsError& err) {
                st.addError(t.entry.name + QStringLiteral(": ") + err.message());
            }
        }
    }
}

void runDelete(JobState& st, const BackendPtr& b, const QString& dir, const QList<Entry>& items, bool toTrash)
{
    auto* local = toTrash ? dynamic_cast<LocalBackend*>(b.get()) : nullptr;
    for (const Entry& e : items) {
        if (st.cancel)
            throw Cancelled();
        st.setCurrent(e.name);
        try {
            if (local) {
                for (const QString& n : realNames(e))
                    local->moveToTrash(joinPath(dir, n));
            } else {
                removeEntry(*b, dir, e);
            }
        } catch (const Cancelled&) {
            throw;
        } catch (const FsError& err) {
            st.addError(e.name + QStringLiteral(": ") + err.message());
        }
    }
}
