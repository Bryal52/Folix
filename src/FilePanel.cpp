#include "FilePanel.h"

#include "Devices.h"
#include "Dialogs.h"
#include "FileModel.h"
#include "FileTypes.h"
#include "Log.h"
#include "Platform.h"
#include "Preview.h"
#include "SizeIndex.h"
#include "Tools.h"

#include <QApplication>
#include <QCursor>
#include <QDrag>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPointer>
#include <QProcess>
#include <QSignalBlocker>
#include <QStyle>
#include <QThreadPool>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

const char* kPanelMime = "application/x-folix-panel";

struct ListResult {
    bool ok = false;
    QString error;
    QList<Entry> entries;
};

} // namespace

// ---------------------------------------------------------------------------

void DeviceCombo::showPopup()
{
    emit aboutToShowPopup();
    QComboBox::showPopup();
}

// ---------------------------------------------------------------------------

bool PanelView::event(QEvent* event)
{
    // Tab switches panels instead of moving keyboard focus around.
    if (event->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(event);
        if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
            emit switchRequested();
            return true;
        }
    }
    return QTreeView::event(event);
}

void PanelView::keyPressEvent(QKeyEvent* event)
{
    const bool cmd = event->modifiers() & Qt::ControlModifier;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit openRequested();
        return;
    case Qt::Key_Backspace:
        if (event->modifiers() == Qt::NoModifier) {
            emit upRequested();
            return;
        }
        break;
    case Qt::Key_Space:
        if (event->modifiers() == Qt::NoModifier) {
            emit sizeRequested();
            return;
        }
        break;
    case Qt::Key_Down:
        if (cmd) {
            emit openRequested();
            return;
        }
        break;
    case Qt::Key_Up:
        if (cmd) {
            emit upRequested();
            return;
        }
        break;
    default:
        break;
    }
    QTreeView::keyPressEvent(event);
}

void PanelView::focusInEvent(QFocusEvent* event)
{
    QTreeView::focusInEvent(event);
    emit focused();
}

void PanelView::startDrag(Qt::DropActions)
{
    auto* panel = qobject_cast<FilePanel*>(parentWidget());
    if (!panel)
        return;
    const QList<Entry> items = panel->selectedEntries();
    if (items.isEmpty())
        return;

    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kPanelMime), QByteArray::number(quintptr(this)));
    if (panel->backend() && panel->backend()->isLocal()) {
        QList<QUrl> urls;
        for (const Entry& e : items) {
            for (const QString& n : realNames(e))
                urls << QUrl::fromLocalFile(joinPath(panel->dir(), n));
        }
        mime->setUrls(urls);   // lets you drag files out to Finder
    }
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(fileIcon(classify(items.first()).cat).pixmap(40, 40));
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
}

bool PanelView::acceptable(const QMimeData* mime) const
{
    if (mime->hasFormat(QString::fromLatin1(kPanelMime)))
        return mime->data(QString::fromLatin1(kPanelMime)).toULongLong() != quintptr(this);
    if (!mime->hasUrls())
        return false;
    for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile())
            return false;
    }
    return true;
}

void PanelView::dragEnterEvent(QDragEnterEvent* event)
{
    if (!acceptable(event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction((event->modifiers() & Qt::ControlModifier) ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
}

void PanelView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!acceptable(event->mimeData())) {
        event->ignore();
        return;
    }
    event->setDropAction((event->modifiers() & Qt::ControlModifier) ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
}

void PanelView::dropEvent(QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!acceptable(mime)) {
        event->ignore();
        return;
    }
    QObject* source = nullptr;
    QList<QUrl> urls;
    if (mime->hasFormat(QString::fromLatin1(kPanelMime)))
        source = reinterpret_cast<QObject*>(quintptr(mime->data(QString::fromLatin1(kPanelMime)).toULongLong()));
    else
        urls = mime->urls();
    const bool move = event->modifiers() & Qt::ControlModifier;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    // Run the copy after the drag session has finished.
    QTimer::singleShot(0, this, [this, source, urls, move] { emit dropped(source, urls, move); });
}

// ---------------------------------------------------------------------------

FilePanel::FilePanel(QWidget* parent) : QWidget(parent)
{
    m_pool = new QThreadPool;   // intentionally never deleted: quitting must not wait for slow size counts
    m_pool->setMaxThreadCount(2);

    m_accent = new QFrame(this);
    m_accent->setFixedHeight(3);

    m_combo = new DeviceCombo(this);
    m_combo->setIconSize(QSize(18, 18));
    m_combo->setMinimumWidth(190);
    m_combo->setMaxVisibleItems(30);
    m_combo->setFocusPolicy(Qt::NoFocus);

    m_edit = new QToolButton(this);
    m_edit->setText(QStringLiteral("✎"));
    m_edit->setToolTip(QStringLiteral("Изменить или удалить подключение"));
    m_edit->setAutoRaise(true);
    m_edit->setFocusPolicy(Qt::NoFocus);

    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(QStringLiteral("Путь"));

    m_refresh = new QToolButton(this);
    m_refresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_refresh->setToolTip(Platform::keys(QStringLiteral("Обновить (⌘R)")));
    m_refresh->setAutoRaise(true);
    m_refresh->setFocusPolicy(Qt::NoFocus);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(8, 6, 8, 6);
    top->setSpacing(6);
    top->addWidget(m_combo);
    top->addWidget(m_edit);
    top->addWidget(m_path, 1);
    top->addWidget(m_refresh);

    m_model = new FileModel(this);
    m_view = new PanelView(this);
    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setItemsExpandable(false);
    m_view->setUniformRowHeights(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setIconSize(QSize(20, 20));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(false);
    m_view->setDragDropMode(QAbstractItemView::DragDrop);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(FileModel::Name, Qt::AscendingOrder);

    QHeaderView* header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(FileModel::Name, QHeaderView::Stretch);
    header->setSectionResizeMode(FileModel::Type, QHeaderView::Interactive);
    header->setSectionResizeMode(FileModel::Size, QHeaderView::Interactive);
    header->setSectionResizeMode(FileModel::Date, QHeaderView::Interactive);
    header->resizeSection(FileModel::Type, 170);
    header->resizeSection(FileModel::Size, 90);
    header->resizeSection(FileModel::Date, 128);
    header->setSectionsMovable(false);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_status->setContentsMargins(10, 4, 10, 6);
    QFont sf = m_status->font();
    sf.setPointSizeF(sf.pointSizeF() - 1);
    m_status->setFont(sf);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_accent);
    layout->addLayout(top);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);

    connect(m_view, &PanelView::openRequested, this, &FilePanel::openCurrent);
    connect(m_view, &QTreeView::doubleClicked, this, &FilePanel::openCurrent);
    connect(m_view, &PanelView::upRequested, this, &FilePanel::goUp);
    connect(m_view, &PanelView::switchRequested, this, &FilePanel::switchRequested);
    connect(m_view, &PanelView::sizeRequested, this, &FilePanel::calcSelectedSizes);
    connect(m_view, &PanelView::focused, this, [this] { emit activated(this); });
    connect(m_view, &PanelView::dropped, this, [this](QObject* src, const QList<QUrl>& urls, bool move) {
        emit dropRequested(this, src, urls, move);
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FilePanel::updateStatus);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        hideHover();
        const QModelIndex idx = m_view->indexAt(pos);
        const bool onItem = idx.isValid() && !m_model->isParentRow(idx.row());
        if (onItem && !m_view->selectionModel()->isRowSelected(idx.row(), {}))
            m_view->setCurrentIndex(idx);   // right-click selects, like Finder
        else if (!onItem)
            m_view->clearSelection();
        emit activated(this);
        emit contextMenuRequested(this, m_view->viewport()->mapToGlobal(pos), onItem);
    });

    connect(m_combo, &DeviceCombo::aboutToShowPopup, this, &FilePanel::refreshDevices);
    connect(m_combo, &QComboBox::activated, this, &FilePanel::deviceChosen);
    connect(m_edit, &QToolButton::clicked, this, [this] { emit editConnectionRequested(this); });
    connect(m_refresh, &QToolButton::clicked, this, [this] { reload(); });
    connect(m_path, &QLineEdit::returnPressed, this, [this] {
        QString p = m_path->text().trimmed();
        if (p.startsWith(QLatin1Char('~')) && m_backend && m_backend->isLocal())
            p = m_backend->homePath() + p.mid(1);
        if (!p.isEmpty())
            navigate(p);
        m_view->setFocus();
    });
    connect(&Devices::instance(), &Devices::profilesChanged, this, &FilePanel::refreshDevices);
    connect(&SizeIndex::instance(), &SizeIndex::changed, this, [this](const QString& id) {
        if (!m_backend || m_backend->id() != id)
            return;
        m_model->applyDirSizes(indexSizes(m_model->entries()));
        updateStatus();
    });
    connect(&SizeIndex::instance(), &SizeIndex::progress, this, [this](const QString& id) {
        if (m_backend && m_backend->id() == id)
            updateStatus();
    });

    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(500);
    connect(&m_hoverTimer, &QTimer::timeout, this, &FilePanel::showHover);
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
    m_view->installEventFilter(this);

    setActive(false);
}

void FilePanel::setLocation(const QString& key, const QString& dir, bool interactive)
{
    BackendPtr b = Devices::instance().backend(key);
    if (!b)
        return;
    m_key = key;
    m_backend = b;
    m_dir.clear();
    m_free = -1;
    m_model->setEntries({}, false);
    refreshDevices();
    if (!b->isLocal())
        SizeIndex::instance().ensure(b);
    navigate(dir.isEmpty() ? b->homePath() : dir, {}, interactive);
}

void FilePanel::navigate(const QString& dir, const QString& selectName, bool interactive)
{
    if (!m_backend)
        return;
    hideHover();
    if (m_sizeCancel)
        *m_sizeCancel = true;

    const quint64 gen = ++m_gen;
    const BackendPtr b = m_backend;
    m_status->setText(QStringLiteral("Загрузка…"));

    auto* watcher = new QFutureWatcher<ListResult>(this);
    connect(watcher, &QFutureWatcher<ListResult>::finished, this, [=, this] {
        const ListResult r = watcher->result();
        watcher->deleteLater();
        if (gen != m_gen)
            return;
        if (!r.ok) {
            if (m_dir.isEmpty())
                m_path->setText(dir);
            m_status->setText(QStringLiteral("⚠︎ ") + r.error);
            if (!interactive)
                Log::warning(QStringLiteral("Папка"), dir + QStringLiteral(": ") + r.error);
            if (interactive)
                Dialogs::error(window(), QStringLiteral("Не удалось открыть папку"), r.error);
            return;
        }
        m_dir = dir;
        m_path->setText(dir);
        QList<Entry> entries = r.entries;
        if (b->isLocal()) {
            const QString root = SizeIndex::localRootFor(dir);
            if (!root.isEmpty())
                SizeIndex::instance().ensure(b, root);   // home folder or an external drive
        } else {
            SizeIndex::instance().ensure(b);   // rescans if the data is older than a day
        }
        const QHash<QString, qint64> sizes = indexSizes(entries);
        for (Entry& e : entries) {
            if (e.isDir && sizes.contains(e.name))
                e.size = sizes.value(e.name);
        }
        m_model->setLocalDir(b->isLocal() ? dir : QString());
        m_model->setEntries(entries, dir != QLatin1String("/"));
        if (m_model->rowCount() > 0) {
            int row = selectName.isEmpty() ? -1 : m_model->rowOfName(selectName);
            if (row < 0)
                row = 0;
            const QModelIndex idx = m_model->index(row, 0);
            m_view->setCurrentIndex(idx);
            m_view->scrollTo(idx);
        }
        updateFreeSpace();
        updateStatus();
        emit locationChanged();
    });
    watcher->setFuture(QtConcurrent::run([b, dir] {
        ListResult r;
        try {
            r.entries = b->list(dir);
            groupSplitParts(r.entries);
            r.ok = true;
        } catch (const FsError& e) {
            r.error = e.message();
        }
        return r;
    }));
}

void FilePanel::reload(const QString& selectName)
{
    if (!m_backend)
        return;
    if (m_dir.isEmpty()) {
        navigate(Devices::instance().startPath(m_key), selectName);
        return;
    }
    QString keep = selectName;
    if (keep.isEmpty() && hasCurrent())
        keep = currentEntry().name;
    navigate(m_dir, keep, false);
}

void FilePanel::goUp()
{
    if (m_dir.isEmpty() || m_dir == QLatin1String("/"))
        return;
    navigate(parentPath(m_dir), baseName(m_dir));
}

void FilePanel::openCurrent()
{
    const QModelIndex idx = m_view->currentIndex();
    if (!idx.isValid())
        return;
    if (m_model->isParentRow(idx.row())) {
        goUp();
        return;
    }
    const Entry e = m_model->entryAt(idx.row());
    if (e.isDir && m_backend->isLocal() && e.name.endsWith(QLatin1String(".app"))) {
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), {joinPath(m_dir, e.name)});
        return;
    }
    if (e.isDir)
        navigate(joinPath(m_dir, e.name));
    else
        emit openFile(this, e);
}

QList<Entry> FilePanel::selectedEntries() const
{
    QList<Entry> out;
    for (const QModelIndex& idx : m_view->selectionModel()->selectedRows()) {
        if (!m_model->isParentRow(idx.row()))
            out.append(m_model->entryAt(idx.row()));
    }
    if (out.isEmpty() && hasCurrent())
        out.append(currentEntry());
    return out;
}

QList<Entry> FilePanel::entries() const
{
    return m_model->entries();
}

bool FilePanel::hasCurrent() const
{
    const QModelIndex idx = m_view->currentIndex();
    return idx.isValid() && !m_model->isParentRow(idx.row());
}

Entry FilePanel::currentEntry() const
{
    return m_model->entryAt(m_view->currentIndex().row());
}

bool FilePanel::contains(const QString& name) const
{
    return m_model->contains(name);
}

void FilePanel::setActive(bool active)
{
    m_accent->setStyleSheet(active ? QStringLiteral("background: palette(highlight);")
                                   : QStringLiteral("background: transparent;"));
}

void FilePanel::setShowHidden(bool show)
{
    m_model->setShowHidden(show);
    updateStatus();
}

void FilePanel::refreshDevices()
{
    const QSignalBlocker block(m_combo);
    m_combo->clear();
    int current = -1;
    for (const DeviceChoice& c : Devices::instance().choices()) {
        if (c.separatorBefore && m_combo->count() > 0)
            m_combo->insertSeparator(m_combo->count());
        m_combo->addItem(c.icon, c.label, c.key);
        const int idx = m_combo->count() - 1;
        m_combo->setItemData(idx, c.hint, Qt::UserRole + 1);
        m_combo->setItemData(idx, c.usable, Qt::UserRole + 2);
        if (c.key == m_key)
            current = idx;
    }
    if (current < 0 && m_backend) {
        m_combo->insertItem(0, deviceIcon(m_backend->kind()), m_backend->title(), m_key);
        m_combo->setItemData(0, true, Qt::UserRole + 2);
        current = 0;
    }
    m_combo->setCurrentIndex(current);
    m_edit->setVisible(m_key.startsWith(QLatin1String("ftp:")));
}

void FilePanel::deviceChosen(int index)
{
    const QString key = m_combo->itemData(index).toString();
    emit activated(this);
    if (key == QLatin1String("action:new")) {
        refreshDevices();
        emit newConnectionRequested(this);
        return;
    }
    if (!m_combo->itemData(index, Qt::UserRole + 2).toBool()) {
        const QString hint = m_combo->itemData(index, Qt::UserRole + 1).toString();
        refreshDevices();
        Dialogs::info(window(), QStringLiteral("Устройство пока не готово"), hint);
        return;
    }
    setLocation(key, Devices::instance().startPath(key));
    m_view->setFocus();
}

void FilePanel::updateStatus()
{
    int dirs = 0, files = 0;
    qint64 filesSize = 0;
    for (const Entry& e : m_model->entries()) {
        if (e.isDir) {
            ++dirs;
        } else {
            ++files;
            filesSize += qMax<qint64>(0, e.size);
        }
    }
    QString s = QStringLiteral("Папок: %1, файлов: %2 (%3)").arg(dirs).arg(files).arg(Tools::humanSize(filesSize));

    int selCount = 0;
    qint64 selSize = 0;
    for (const QModelIndex& idx : m_view->selectionModel()->selectedRows()) {
        if (m_model->isParentRow(idx.row()))
            continue;
        ++selCount;
        selSize += qMax<qint64>(0, m_model->entryAt(idx.row()).size);
    }
    if (selCount > 1)
        s += QStringLiteral(" · выделено: %1 (%2)").arg(selCount).arg(Tools::humanSize(selSize));
    if (m_free >= 0)
        s += QStringLiteral(" · свободно ") + Tools::humanSize(m_free);
    if (m_backend && !m_dir.isEmpty()) {
        const SizeIndex& index = SizeIndex::instance();
        const QString id = m_backend->id();
        if (index.scanning(id, m_dir)) {
            s += QStringLiteral(" · сканирую: %1 файлов…")
                     .arg(QLocale(QLocale::Russian).toString(index.scannedFiles(id, m_dir)));
        } else if (index.scannedAt(id, m_dir).isValid()) {
            const QDateTime at = index.scannedAt(id, m_dir);
            s += m_backend->isLocal() ? QStringLiteral(" · размеры папок обновляются сами")
                                      : QStringLiteral(" · размеры папок от ")
                    + at.toString(at.date() == QDate::currentDate() ? QStringLiteral("HH:mm") : QStringLiteral("dd.MM HH:mm"));
        } else if (!index.lastError(id, m_dir).isEmpty()) {
            s += QStringLiteral(" · скан не удался");
        }
    }
    m_status->setText(s);
}

void FilePanel::updateFreeSpace()
{
    const BackendPtr b = m_backend;
    const QString dir = m_dir;
    const quint64 gen = m_gen;
    auto* watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this, [this, watcher, gen] {
        const qint64 free = watcher->result();
        watcher->deleteLater();
        if (gen != m_gen)
            return;
        m_free = free;
        updateStatus();
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [b, dir]() -> qint64 {
        try {
            return b->freeSpace(dir);
        } catch (const FsError&) {
            return -1;
        }
    }));
}

void FilePanel::calcSelectedSizes()
{
    QList<Entry> dirs;
    for (const Entry& e : selectedEntries()) {
        if (e.isDir)
            dirs.append(e);
    }
    calcSizes(dirs);
}

QHash<QString, qint64> FilePanel::indexSizes(const QList<Entry>& entries) const
{
    QHash<QString, qint64> out;
    if (!m_backend)
        return out;
    const SizeIndex& index = SizeIndex::instance();
    const QString id = m_backend->id();
    const bool scanning = index.scanning(id, m_dir);
    for (const Entry& e : entries) {
        if (!e.isDir)
            continue;
        const qint64 s = index.size(id, joinPath(m_dir, e.name));
        if (s >= 0)
            out.insert(e.name, s);
        else if (scanning)
            out.insert(e.name, -2);
    }
    return out;
}

void FilePanel::calcAllSizes()
{
    QList<Entry> dirs;
    for (const Entry& e : m_model->entries()) {
        if (e.isDir && e.size < 0)
            dirs.append(e);
    }
    calcSizes(dirs);
}

void FilePanel::calcSizes(const QList<Entry>& dirs)
{
    if (dirs.isEmpty() || !m_backend)
        return;
    {
        SizeIndex& index = SizeIndex::instance();
        if (index.scanning(m_backend->id(), m_dir) && index.has(m_backend->id(), m_dir))
            return;   // the running scan will fill these in
        if (index.has(m_backend->id(), m_dir)) {
            QStringList names;
            for (const Entry& e : dirs) {
                names << e.name;
                m_model->setDirSize(e.name, -2);
            }
            index.refresh(m_backend, m_dir, names);
            return;
        }
    }
    if (!m_sizeCancel || *m_sizeCancel)
        m_sizeCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_sizeCancel;
    const BackendPtr b = m_backend;
    const QString base = m_dir;
    const quint64 gen = m_gen;
    QPointer<FilePanel> self(this);

    for (const Entry& e : dirs) {
        m_model->setDirSize(e.name, -2);
        const QString name = e.name;
        m_pool->start([=] {
            qint64 size = -1;
            try {
                if (!*cancel)
                    size = b->dirSize(joinPath(base, name), cancel.get());
            } catch (const FsError&) {
                size = -1;
            }
            QMetaObject::invokeMethod(qApp, [self, gen, name, size] {
                if (!self || gen != self->m_gen)
                    return;
                self->m_model->setDirSize(name, size);
                self->updateStatus();
            }, Qt::QueuedConnection);
        });
    }
}

QByteArray FilePanel::saveHeader() const
{
    return m_view->header()->saveState();
}

void FilePanel::restoreHeader(const QByteArray& state)
{
    if (!state.isEmpty())
        m_view->header()->restoreState(state);
    m_view->header()->setSectionResizeMode(FileModel::Name, QHeaderView::Stretch);
}

bool FilePanel::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_view->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(event);
            const QModelIndex idx = m_view->indexAt(me->position().toPoint());
            const int row = idx.isValid() ? idx.row() : -1;
            if (row != m_hoverRow) {
                m_hoverRow = row;
                PreviewPopup::instance()->hidePopup();
                if (row >= 0)
                    m_hoverTimer.start();
                else
                    m_hoverTimer.stop();
            }
            break;
        }
        case QEvent::Leave:
        case QEvent::Wheel:
        case QEvent::MouseButtonPress:
        case QEvent::Hide:
            hideHover();
            break;
        default:
            break;
        }
    } else if (obj == m_view && event->type() == QEvent::KeyPress) {
        hideHover();
    }
    return QWidget::eventFilter(obj, event);
}

void FilePanel::showHover()
{
    if (m_hoverRow < 0 || m_hoverRow >= m_model->rowCount() || m_model->isParentRow(m_hoverRow))
        return;
    if (!window()->isActiveWindow() || !m_backend)
        return;
    const Entry e = m_model->entryAt(m_hoverRow);
    if (Preview::wants(e))
        PreviewPopup::instance()->showFor(m_backend, m_dir, e, QCursor::pos());
}

void FilePanel::hideHover()
{
    m_hoverTimer.stop();
    m_hoverRow = -1;
    PreviewPopup::instance()->hidePopup();
}
