#include "InfoDialog.h"

#include "Devices.h"
#include "FileTypes.h"
#include "Preview.h"
#include "SizeIndex.h"
#include "Tools.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

struct TreeStats {
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 dirs = 0;
    bool ok = true;
};

QString exactSize(qint64 bytes)
{
    static const QLocale ru(QLocale::Russian);
    if (bytes < 1024)
        return Tools::humanSize(bytes);
    return QStringLiteral("%1 (%2 байт)").arg(Tools::humanSize(bytes), ru.toString(bytes));
}

QString formatDate(const QDateTime& dt)
{
    if (!dt.isValid())
        return QStringLiteral("—");
    static const QLocale ru(QLocale::Russian);
    return ru.toString(dt, QStringLiteral("d MMMM yyyy, HH:mm"));
}

QString plural(qint64 n, const char* one, const char* few, const char* many)
{
    const qint64 m10 = n % 10, m100 = n % 100;
    const char* w = (m10 == 1 && m100 != 11) ? one
        : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many;
    return QLocale(QLocale::Russian).toString(n) + QLatin1Char(' ') + QString::fromUtf8(w);
}

QString permissions(const QFileInfo& fi)
{
    const auto p = fi.permissions();
    auto rwx = [&](QFile::Permission r, QFile::Permission w, QFile::Permission x) {
        return QString(p & r ? QLatin1Char('r') : QLatin1Char('-')) + (p & w ? QLatin1Char('w') : QLatin1Char('-'))
            + (p & x ? QLatin1Char('x') : QLatin1Char('-'));
    };
    return rwx(QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner) + rwx(QFile::ReadGroup, QFile::WriteGroup, QFile::ExeGroup)
        + rwx(QFile::ReadOther, QFile::WriteOther, QFile::ExeOther)
        + QStringLiteral("  (владелец: %1, группа: %2)").arg(fi.owner(), fi.group());
}

} // namespace

InfoDialog::InfoDialog(QWidget* parent, const BackendPtr& b, const QString& dir, const QList<Entry>& items)
    : QDialog(parent), m_backend(b), m_dir(dir), m_items(items)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(480);

    const bool self = items.isEmpty();
    m_self = self;
    Entry single;
    if (self) {
        single.name = baseName(dir).isEmpty() ? dir : baseName(dir);
        single.isDir = true;
        m_items = {single};
    }
    const bool one = m_items.size() == 1;
    const Entry& first = m_items.first();
    const QString firstPath = self ? dir : joinPath(dir, first.name);

    for (const Entry& e : std::as_const(m_items)) {
        if (e.isDir)
            ++m_selectedDirs;
        else {
            ++m_selectedFiles;
            m_fileBytes += qMax<qint64>(0, e.size);
        }
    }

    // Header: big icon, name, short summary
    const TypeInfo type = classify(first);
    auto* icon = new QLabel(this);
    icon->setPixmap((one ? fileIcon(type.cat) : fileIcon(FileCat::Folder)).pixmap(64, 64));
    auto* name = new QLabel(one ? first.name : QStringLiteral("Выбрано: %1").arg(m_items.size()), this);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name->setWordWrap(true);
    QFont nf = name->font();
    nf.setBold(true);
    nf.setPointSizeF(nf.pointSizeF() + 2);
    name->setFont(nf);
    m_headerSize = new QLabel(this);
    m_headerSize->setForegroundRole(QPalette::PlaceholderText);
    auto* titles = new QVBoxLayout;
    titles->addWidget(name);
    titles->addWidget(m_headerSize);
    titles->addStretch();
    auto* header = new QHBoxLayout;
    header->addWidget(icon, 0, Qt::AlignTop);
    header->addSpacing(8);
    header->addLayout(titles, 1);

    m_form = new QFormLayout;
    m_form->setLabelAlignment(Qt::AlignRight);
    m_form->setHorizontalSpacing(14);

    if (one)
        addRow(QStringLiteral("Вид:"), type.description);
    else
        addRow(QStringLiteral("Состав:"), plural(m_selectedFiles, "файл", "файла", "файлов") + QStringLiteral(", ")
                                               + plural(m_selectedDirs, "папка", "папки", "папок"));
    addRow(QStringLiteral("Устройство:"), b->title());
    addRow(QStringLiteral("Где:"), self ? parentPath(dir) : dir)->setWordWrap(true);
    m_size = addRow(QStringLiteral("Размер:"), QString());
    if (m_selectedDirs > 0)
        m_contents = addRow(QStringLiteral("Содержит:"), QStringLiteral("считаю…"));

    if (one) {
        if (b->isLocal()) {
            const QFileInfo fi(firstPath);
            addRow(QStringLiteral("Создан:"), formatDate(fi.birthTime()));
            addRow(QStringLiteral("Изменён:"), formatDate(fi.lastModified()));
            addRow(QStringLiteral("Открыт:"), formatDate(fi.lastRead()));
            addRow(QStringLiteral("Права:"), permissions(fi));
            if (fi.isHidden())
                addRow(QStringLiteral("Скрытый:"), QStringLiteral("да"));
        } else {
            addRow(QStringLiteral("Изменён:"), formatDate(first.mtime));
        }
        if (!first.parts.isEmpty())
            addRow(QStringLiteral("Части:"), first.parts.join(QLatin1Char('\n')));
    }

    // Free space on the device
    QLabel* freeLabel = addRow(QStringLiteral("Свободно:"), QStringLiteral("…"));
    {
        auto* w = new QFutureWatcher<qint64>(this);
        connect(w, &QFutureWatcher<qint64>::finished, this, [w, freeLabel] {
            const qint64 f = w->result();
            freeLabel->setText(f >= 0 ? Tools::humanSize(f) : QStringLiteral("неизвестно"));
            w->deleteLater();
        });
        const QString where = dir;
        w->setFuture(QtConcurrent::run([b, where]() -> qint64 {
            try {
                return b->freeSpace(where);
            } catch (const FsError&) {
                return -1;
            }
        }));
    }

    // Resolution / duration for photos and videos, title for game folders
    if (one && !self && Preview::wants(first)) {
        QLabel* media = addRow(QStringLiteral("Подробно:"), QStringLiteral("…"));
        media->setWordWrap(true);
        auto* w = new QFutureWatcher<PreviewData>(this);
        connect(w, &QFutureWatcher<PreviewData>::finished, this, [w, media] {
            const PreviewData d = w->result();
            QStringList parts;
            if (!d.title.isEmpty())
                parts << d.title;
            parts += d.lines;
            media->setText(parts.isEmpty() ? QStringLiteral("—") : parts.join(QStringLiteral(" · ")));
            w->deleteLater();
        });
        w->setFuture(QtConcurrent::run([b, dir, first] { return Preview::build(b, dir, first); }));
    }

    auto* buttons = new QDialogButtonBox(this);
    if (b->isLocal()) {
        QPushButton* finder = buttons->addButton(QStringLiteral("Показать в Finder"), QDialogButtonBox::ActionRole);
        finder->setAutoDefault(false);
        connect(finder, &QPushButton::clicked, this, [firstPath] {
            QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), firstPath});
        });
    }
    QPushButton* close = buttons->addButton(QStringLiteral("Закрыть"), QDialogButtonBox::RejectRole);
    close->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 14);
    layout->setSpacing(12);
    layout->addLayout(header);
    layout->addLayout(m_form);
    layout->addWidget(buttons);

    setWindowTitle(one ? QStringLiteral("Свойства: ") + first.name : QStringLiteral("Свойства"));
    startCounting();
}

InfoDialog::~InfoDialog()
{
    *m_cancel = true;
}

QLabel* InfoDialog::addRow(const QString& title, const QString& value)
{
    auto* label = new QLabel(value, this);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_form->addRow(title, label);
    return label;
}

void InfoDialog::startCounting()
{
    if (m_selectedDirs == 0) {
        m_size->setText(exactSize(m_fileBytes));
        m_headerSize->setText(Tools::humanSize(m_fileBytes));
        return;
    }

    // Show what the size index already knows while the exact count runs.
    qint64 known = m_fileBytes;
    bool allKnown = true;
    for (const Entry& e : std::as_const(m_items)) {
        if (!e.isDir)
            continue;
        const QString path = pathOf(e);
        const qint64 s = SizeIndex::instance().size(m_backend->id(), path);
        if (s >= 0)
            known += s;
        else
            allKnown = false;
    }
    m_size->setText(allKnown ? exactSize(known) + QStringLiteral(" · уточняю…") : QStringLiteral("считаю…"));
    m_headerSize->setText(allKnown ? Tools::humanSize(known) : QStringLiteral("считаю…"));

    QStringList dirs;
    for (const Entry& e : std::as_const(m_items)) {
        if (e.isDir)
            dirs << pathOf(e);
    }
    const BackendPtr b = m_backend;
    const auto cancel = m_cancel;
    const auto seen = m_seenFiles;
    auto* w = new QFutureWatcher<TreeStats>(this);
    connect(w, &QFutureWatcher<TreeStats>::finished, this, [this, w] {
        const TreeStats t = w->result();
        w->deleteLater();
        m_tick.stop();
        if (!t.ok) {
            m_size->setText(QStringLiteral("не удалось посчитать"));
            if (m_contents)
                m_contents->setText(QStringLiteral("—"));
            return;
        }
        const qint64 total = t.bytes + m_fileBytes;
        m_size->setText(exactSize(total));
        m_headerSize->setText(Tools::humanSize(total));
        if (m_contents)
            m_contents->setText(plural(t.files, "файл", "файла", "файлов") + QStringLiteral(", ")
                                + plural(t.dirs, "папка", "папки", "папок"));
    });
    w->setFuture(QtConcurrent::run([b, dirs, cancel, seen] {
        TreeStats t;
        qint64 before = 0;
        try {
            for (const QString& d : dirs) {
                std::atomic<qint64> maxFiles{0};   // FTP reports from several threads
                const SizeMap m = b->scanTree(d, cancel.get(), [&maxFiles, seen, before](qint64 n) {
                    qint64 cur = maxFiles.load();
                    while (n > cur && !maxFiles.compare_exchange_weak(cur, n)) {
                    }
                    *seen = before + maxFiles.load();
                });
                t.bytes += m.value(d);
                t.dirs += m.size() - 1;
                t.files += maxFiles.load();
                before += maxFiles.load();
            }
        } catch (const FsError&) {
            t.ok = false;
        }
        return t;
    }));

    m_tick.setInterval(300);
    connect(&m_tick, &QTimer::timeout, this, &InfoDialog::updateCounting);
    m_tick.start();
}

void InfoDialog::updateCounting()
{
    if (m_contents)
        m_contents->setText(QStringLiteral("считаю… ") + plural(m_seenFiles->load(), "файл", "файла", "файлов"));
}
