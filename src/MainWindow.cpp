#include "MainWindow.h"

#include "AutoOpen.h"
#include "ConnectionDialog.h"
#include "Devices.h"
#include "Dialogs.h"
#include "FilePanel.h"
#include "FullDiskAccess.h"
#include "InfoDialog.h"
#include "JobDialog.h"
#include "LogViewer.h"
#include "LocalBackend.h"
#include "Operations.h"
#include "Preview.h"
#include "Protection.h"
#include "SizeIndex.h"
#include "Tools.h"
#include "Viewer.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

QString macKey(QStandardPaths::StandardLocation loc)
{
    return QStringLiteral("mac:") + QStandardPaths::writableLocation(loc);
}

QString describe(const QList<Entry>& items)
{
    if (items.size() == 1)
        return QStringLiteral("«%1»").arg(items.first().name);
    return QStringLiteral("выбранные объекты (%1)").arg(items.size());
}

QIcon appIcon()
{
    return QIcon(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/Folix.icns"));
}

void reportErrors(QWidget* parent, const QString& title, const JobResult& r)
{
    if (r.errors.isEmpty())
        return;
    QString text = r.errors.first();
    if (r.errors.size() > 1)
        text += QStringLiteral("\n\n…и ещё ошибок: %1. Подробности — по кнопке ниже.").arg(r.errors.size() - 1);
    Dialogs::error(parent, title, text, r.errors.size() > 1 ? r.errors : QStringList());
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Folix"));
    setUnifiedTitleAndToolBarOnMac(true);

    m_left = new FilePanel;
    m_right = new FilePanel;
    m_active = m_left;

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->setObjectName(QStringLiteral("panels"));
    splitter->addWidget(m_left);
    splitter->addWidget(m_right);
    splitter->setChildrenCollapsible(false);

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter, 1);
    layout->addWidget(buildFunctionBar());
    setCentralWidget(central);

    for (FilePanel* p : {m_left, m_right}) {
        connect(p, &FilePanel::activated, this, &MainWindow::setActive);
        connect(p, &FilePanel::openFile, this, &MainWindow::openFile);
        connect(p, &FilePanel::switchRequested, this, [this, p] {
            FilePanel* o = other(p);
            setActive(o);
            o->view()->setFocus();
        });
        connect(p, &FilePanel::newConnectionRequested, this, &MainWindow::newConnection);
        connect(p, &FilePanel::editConnectionRequested, this, &MainWindow::editConnection);
        connect(p, &FilePanel::dropRequested, this, &MainWindow::handleDrop);
        connect(p, &FilePanel::contextMenuRequested, this, &MainWindow::showContextMenu);
    }
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [](Qt::ApplicationState s) {
        if (s != Qt::ApplicationActive)
            PreviewPopup::instance()->hidePopup();
    });

    buildMenus();
    restoreState();
    startLocalServer();
    AutoOpen::syncOnStartup();

    // The status bar only appears while it has something to say.
    statusBar()->hide();
    connect(statusBar(), &QStatusBar::messageChanged, statusBar(),
            [this](const QString& msg) { statusBar()->setVisible(!msg.isEmpty()); });

    // Ask for Full Disk Access before any protected folder is touched, so macOS does not
    // fire a separate prompt for every folder; then open the panels and scan the home folder.
    QTimer::singleShot(0, this, [this] {
        FullDiskAccess::ask(this);
        restorePanels();
        m_ready = true;
        if (!m_pendingOpen.isEmpty())
            openExternal(m_pendingOpen);
        // Folder sizes of the Mac home folder: shown from the cache at once, rescanned in the background.
        SizeIndex::instance().ensure(Devices::instance().local(), QDir::homePath());
    });

    // Start the adb server early so the phone shows up in the device list right away.
    if (!Tools::adb().isEmpty())
        QProcess::startDetached(Tools::adb(), {QStringLiteral("start-server")});
}

void MainWindow::buildMenus()
{
    auto add = [this](QMenu* menu, const QString& text, const QList<QKeySequence>& keys, std::function<void()> fn) {
        QAction* a = menu->addAction(text);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, std::move(fn));
        return a;
    };

    QMenu* file = menuBar()->addMenu(QStringLiteral("Файл"));
    m_actOpen = add(file, QStringLiteral("Открыть"), {QKeySequence(Qt::CTRL | Qt::Key_O)}, [this] { m_active->openCurrent(); });
    m_actView = add(file, QStringLiteral("Просмотр"), {Qt::Key_F3, QKeySequence(Qt::CTRL | Qt::Key_3)}, [this] { view(); });
    m_actInfo = add(file, QStringLiteral("Свойства"), {QKeySequence(Qt::CTRL | Qt::Key_I)}, [this] { showInfo(); });
    file->addSeparator();
    m_actCopyOther = add(file, QStringLiteral("Копировать в другую панель"), {Qt::Key_F5, QKeySequence(Qt::CTRL | Qt::Key_5)},
                         [this] { copyOrMove(false); });
    m_actMoveOther = add(file, QStringLiteral("Переместить в другую панель"), {Qt::Key_F6, QKeySequence(Qt::CTRL | Qt::Key_6)},
                         [this] { copyOrMove(true); });
    m_actNewDir = add(file, QStringLiteral("Новая папка"),
                      {Qt::Key_F7, QKeySequence(Qt::CTRL | Qt::Key_7), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)},
                      [this] { makeDir(); });
    m_actRename = add(file, QStringLiteral("Переименовать"), {QKeySequence(Qt::SHIFT | Qt::Key_F6), Qt::Key_F2},
                      [this] { renameItem(); });
    m_actDelete = add(file, QStringLiteral("Удалить"),
                      {Qt::Key_F8, QKeySequence(Qt::CTRL | Qt::Key_8), QKeySequence(Qt::CTRL | Qt::Key_Backspace)},
                      [this] { removeItems(); });
    file->addSeparator();
    m_actFinder = add(file, QStringLiteral("Показать в Finder"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R)}, [this] {
        FilePanel* p = m_active;
        if (!p->backend() || !p->backend()->isLocal())
            return;
        const QString path = p->hasCurrent() ? joinPath(p->dir(), p->currentEntry().name) : p->dir();
        QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
    });
    add(file, QStringLiteral("Новое FTP-подключение…"), {QKeySequence(Qt::CTRL | Qt::Key_K)},
        [this] { newConnection(m_active); });

    QMenu* edit = menuBar()->addMenu(QStringLiteral("Правка"));
    m_actCopy = add(edit, QStringLiteral("Копировать"), {QKeySequence::Copy}, [this] { copyToClipboard(false); });
    m_actCut = add(edit, QStringLiteral("Вырезать"), {QKeySequence::Cut}, [this] { copyToClipboard(true); });
    m_actPaste = add(edit, QStringLiteral("Вставить"), {QKeySequence::Paste}, [this] { paste(); });
    edit->addSeparator();
    add(edit, QStringLiteral("Выделить всё"), {QKeySequence::SelectAll}, [this] { m_active->view()->selectAll(); });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("Вид"));
    m_actRefresh = add(viewMenu, QStringLiteral("Обновить"), {QKeySequence(Qt::CTRL | Qt::Key_R)}, [this] { m_active->reload(); });
    m_hiddenAction = add(viewMenu, QStringLiteral("Показывать скрытые файлы"),
        {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Greater)},
        [this] {
            m_left->setShowHidden(m_hiddenAction->isChecked());
            m_right->setShowHidden(m_hiddenAction->isChecked());
        });
    m_hiddenAction->setCheckable(true);
    viewMenu->addSeparator();
    add(viewMenu, QStringLiteral("Размер выделенных папок (Пробел)"), {}, [this] { m_active->calcSelectedSizes(); });
    add(viewMenu, QStringLiteral("Размер всех папок"),
        {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_I), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Return)},
        [this] { m_active->calcAllSizes(); });
    add(viewMenu, QStringLiteral("Пересканировать"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)}, [this] {
        const BackendPtr b = m_active->backend();
        if (!b)
            return;
        if (b->isLocal()) {
            const QString root = SizeIndex::localRootFor(m_active->dir());
            if (!root.isEmpty())
                SizeIndex::instance().ensure(b, root, true);
        } else {
            SizeIndex::instance().ensure(b, {}, true);
        }
    });
    viewMenu->addSeparator();
    add(viewMenu, QStringLiteral("Поменять панели местами"), {QKeySequence(Qt::CTRL | Qt::Key_U)},
        [this] { swapPanels(); });

    QMenu* go = menuBar()->addMenu(QStringLiteral("Переход"));
    add(go, QStringLiteral("Наверх"), {QKeySequence(Qt::CTRL | Qt::Key_Up)}, [this] { m_active->goUp(); });
    add(go, QStringLiteral("Домашняя папка"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H)},
        [this] { goPlace(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)); });
    add(go, QStringLiteral("Рабочий стол"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)},
        [this] { goPlace(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)); });
    add(go, QStringLiteral("Загрузки"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L)},
        [this] { goPlace(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)); });

    QMenu* disks = menuBar()->addMenu(QStringLiteral("Диски"));
    connect(disks, &QMenu::aboutToShow, this, [this, disks] { buildDiskMenu(disks); });
    buildDiskMenu(disks);

    QMenu* help = menuBar()->addMenu(QStringLiteral("Справка"));
    QAction* aboutAction = help->addAction(QStringLiteral("О программе Folix"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::about);
    QAction* access = help->addAction(QStringLiteral("Полный доступ к диску…"));
    connect(access, &QAction::triggered, this, [this] {
        if (FullDiskAccess::granted())
            Dialogs::info(this, QStringLiteral("Полный доступ уже есть"),
                          QStringLiteral("macOS не будет спрашивать разрешения для отдельных папок."));
        else
            FullDiskAccess::ask(this, true);
    });
    QAction* keys = help->addAction(QStringLiteral("Горячие клавиши"));
    connect(keys, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::buildDiskMenu(QMenu* menu)
{
    menu->clear();
    QAction* autoOpen = menu->addAction(QStringLiteral("Открывать Folix при подключении дисков"));
    autoOpen->setCheckable(true);
    autoOpen->setChecked(AutoOpen::isEnabled());
    connect(autoOpen, &QAction::toggled, this, [](bool on) { AutoOpen::setEnabled(on); });
    menu->addSeparator();

    const QString dir = m_active ? m_active->dir() : QString();
    const bool onDrive = m_active && m_active->backend() && m_active->backend()->isLocal()
        && dir.startsWith(QLatin1String("/Volumes/"));
    const QString volume = onDrive ? QStringLiteral("/Volumes/") + dir.section(QLatin1Char('/'), 2, 2) : QString();
    const QString name = onDrive ? volume.mid(9) : QString();

    QAction* ignore = menu->addAction(onDrive ? QStringLiteral("Не открывать «%1» автоматически").arg(name)
                                              : QStringLiteral("Не открывать этот диск автоматически"));
    ignore->setEnabled(onDrive);
    connect(ignore, &QAction::triggered, this, [this, volume, name] {
        const QString uuid = AutoOpen::volumeUuid(volume);
        if (uuid.isEmpty()) {
            Dialogs::error(this, QStringLiteral("Не удалось определить диск"), name);
            return;
        }
        AutoOpen::ignoreVolume(uuid);
        Dialogs::info(this, QStringLiteral("Готово"),
                      QStringLiteral("«%1» больше не будет открываться в Folix сам.").arg(name));
    });
    const int ignored = AutoOpen::ignoredCount();
    QAction* reset = menu->addAction(QStringLiteral("Снова открывать все диски (в списке: %1)").arg(ignored));
    reset->setEnabled(ignored > 0);
    connect(reset, &QAction::triggered, this, [] { AutoOpen::clearIgnored(); });
    menu->addSeparator();

    QAction* eject = menu->addAction(onDrive ? QStringLiteral("Извлечь «%1»").arg(name) : QStringLiteral("Извлечь диск"));
    eject->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(eject, &QAction::triggered, this, &MainWindow::ejectDrive);
}

QWidget* MainWindow::buildFunctionBar()
{
    struct Button {
        const char* text;
        const char* tip;
        std::function<void()> fn;
    };
    const Button buttons[] = {
        {"ⓘ  Информация", "⌘I — свойства выделенного", [this] { showInfo(); }},
        {"F3  Просмотр", "F3 или ⌘3", [this] { view(); }},
        {"F5  Копировать", "F5 или ⌘5 — копировать в другую панель", [this] { copyOrMove(false); }},
        {"F6  Переместить", "F6 или ⌘6 — переместить в другую панель", [this] { copyOrMove(true); }},
        {"F7  Новая папка", "F7, ⌘7 или ⇧⌘N", [this] { makeDir(); }},
        {"F8  Удалить", "F8, ⌘8 или ⌘⌫", [this] { removeItems(); }},
        {"⇧F6  Переименовать", "⇧F6 или F2", [this] { renameItem(); }},
    };

    auto* bar = new QWidget;
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 8);
    layout->setSpacing(6);
    for (const Button& b : buttons) {
        auto* btn = new QPushButton(QString::fromUtf8(b.text), bar);
        btn->setToolTip(QString::fromUtf8(b.tip));
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, b.fn);
        layout->addWidget(btn, 1);
    }
    layout->addSpacing(2);
    layout->addWidget(new LogButton(bar));
    return bar;
}

void MainWindow::setActive(FilePanel* panel)
{
    m_active = panel;
    m_left->setActive(panel == m_left);
    m_right->setActive(panel == m_right);
}

FilePanel* MainWindow::other(FilePanel* panel) const
{
    return panel == m_left ? m_right : m_left;
}

void MainWindow::view()
{
    FilePanel* p = m_active;
    if (!p->hasCurrent())
        return;
    const Entry e = p->currentEntry();
    if (e.isDir)
        p->navigate(joinPath(p->dir(), e.name));
    else
        openFile(p, e);
}

void MainWindow::openFile(FilePanel* panel, const Entry& e)
{
    Viewer::open(this, panel->backend(), panel->dir(), e, panel->entries());
}

void MainWindow::copyOrMove(bool move)
{
    if (JobDialog::busy())
        return;
    FilePanel* src = m_active;
    const QList<Entry> items = src->selectedEntries();
    if (items.isEmpty() || !src->backend())
        return;
    transfer(src->backend(), src->dir(), src, other(src), items, move, true);
}

bool MainWindow::transfer(const BackendPtr& srcBackend, const QString& srcDir, FilePanel* srcPanel, FilePanel* dst,
                          QList<Entry> items, bool move, bool ask)
{
    if (!dst->backend() || dst->dir().isEmpty()) {
        Dialogs::error(this, QStringLiteral("Некуда копировать"), QStringLiteral("Откройте папку назначения в другой панели."));
        return false;
    }
    const BackendPtr dstBackend = dst->backend();
    const bool sameDevice = srcBackend->id() == dstBackend->id();
    if (sameDevice && srcDir == dst->dir()) {
        Dialogs::error(this, QStringLiteral("Это та же папка"),
                       QStringLiteral("Откройте в другой панели папку, куда нужно скопировать файлы."));
        return false;
    }
    if (sameDevice) {
        for (const Entry& e : std::as_const(items)) {
            const QString path = joinPath(srcDir, e.name);
            if (e.isDir && (dst->dir() == path || dst->dir().startsWith(path + QLatin1Char('/')))) {
                Dialogs::error(this, QStringLiteral("Нельзя скопировать папку саму в себя"), e.name);
                return false;
            }
        }
    }
    const GuardResult into = guardForWriteInto(dstBackend->kind(), dst->dir());
    if (into.level == Guard::Block) {
        Dialogs::error(this, QStringLiteral("Запись сюда запрещена"),
                       into.reason + QStringLiteral("\n\nFolix защищает эту папку, чтобы не повредить устройство."));
        return false;
    }
    if (move && srcPanel && !guardModify(srcPanel, items, QStringLiteral("Переместить")))
        return false;

    const QString where = dstBackend->title() + QStringLiteral(" — ") + dst->dir();
    if (ask && !Dialogs::confirm(this,
                                 (move ? QStringLiteral("Переместить ") : QStringLiteral("Копировать ")) + describe(items) + QLatin1Char('?'),
                                 QStringLiteral("Куда: ") + where,
                                 move ? QStringLiteral("Переместить") : QStringLiteral("Копировать"))) {
        return false;
    }

    QSet<QString> conflicts;
    for (const Entry& e : std::as_const(items)) {
        if (dst->contains(e.name))
            conflicts.insert(e.name);
    }
    if (!conflicts.isEmpty()) {
        QStringList names = conflicts.values();
        names.sort();
        QString list = names.mid(0, 6).join(QStringLiteral(", "));
        if (names.size() > 6)
            list += QStringLiteral(" и ещё %1").arg(names.size() - 6);
        const int choice = Dialogs::choose(this, QStringLiteral("Такие объекты уже есть"),
                                           QStringLiteral("В папке назначения уже есть: %1.").arg(list),
                                           {QStringLiteral("Заменить"), QStringLiteral("Пропустить")});
        if (choice < 0)
            return false;
        if (choice == 1) {
            items.erase(std::remove_if(items.begin(), items.end(),
                                       [&](const Entry& e) { return conflicts.contains(e.name); }),
                        items.end());
            conflicts.clear();
            if (items.isEmpty())
                return false;
        }
    }

    const TransferRequest req{srcBackend, srcDir, items, dstBackend, dst->dir(), move, conflicts};
    const JobResult r = JobDialog::run(this, move ? QStringLiteral("Перемещение") : QStringLiteral("Копирование"),
                                       [req](JobState& st) { runTransfer(st, req); });
    SizeIndex& index = SizeIndex::instance();
    QStringList names;
    for (const Entry& e : std::as_const(items))
        names << e.name;
    if (sameDevice && move) {
        for (const Entry& e : std::as_const(items))
            index.rename(dstBackend->id(), joinPath(srcDir, e.name), joinPath(dst->dir(), e.name));
        index.refresh(srcBackend, srcDir);
        index.refresh(dstBackend, dst->dir());
    } else {
        index.refresh(dstBackend, dst->dir(), names);
        if (move)
            index.refresh(srcBackend, srcDir);
    }

    dst->reload(items.first().name);
    if (srcPanel && srcPanel != dst)
        srcPanel->reload();
    reportErrors(this, QStringLiteral("Не всё удалось скопировать"), r);
    return r.ok();
}

void MainWindow::transferFromMac(FilePanel* dst, const QList<QUrl>& urls, bool move)
{
    QMap<QString, QList<Entry>> groups;
    for (const QUrl& u : urls) {
        const QFileInfo fi(u.toLocalFile());
        if (!fi.exists())
            continue;
        Entry e;
        e.name = fi.fileName();
        e.isDir = fi.isDir();
        e.hidden = fi.isHidden();
        e.size = e.isDir ? -1 : fi.size();
        e.mtime = fi.lastModified();
        groups[fi.absolutePath()].append(e);
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        FilePanel* srcPanel = nullptr;
        for (FilePanel* p : {m_left, m_right}) {
            if (p->backend() && p->backend()->isLocal() && p->dir() == it.key())
                srcPanel = p;
        }
        transfer(Devices::instance().local(), it.key(), srcPanel, dst, it.value(), move, move);
    }
}

void MainWindow::handleDrop(FilePanel* target, QObject* sourceView, const QList<QUrl>& urls, bool move)
{
    if (JobDialog::busy())
        return;
    setActive(target);
    FilePanel* src = nullptr;
    if (sourceView == m_left->view())
        src = m_left;
    else if (sourceView == m_right->view())
        src = m_right;
    if (src && src != target) {
        const QList<Entry> items = src->selectedEntries();
        if (!items.isEmpty())
            transfer(src->backend(), src->dir(), src, target, items, move, move);
    } else if (!urls.isEmpty()) {
        transferFromMac(target, urls, move);
    }
}

bool MainWindow::guardModify(FilePanel* panel, const QList<Entry>& items, const QString& action)
{
    const DeviceKind kind = panel->backend()->kind();
    QStringList warnings;
    for (const Entry& e : items) {
        for (const QString& n : realNames(e)) {
            const GuardResult g = guardForModify(kind, joinPath(panel->dir(), n));
            if (g.level == Guard::Block) {
                Dialogs::error(this, QStringLiteral("Это действие запрещено"),
                               g.reason + QStringLiteral("\n\nFolix защищает эту папку, чтобы не повредить устройство."));
                return false;
            }
            if (g.level == Guard::Warn)
                warnings << QStringLiteral("«%1»: %2").arg(e.name, g.reason);
        }
    }
    warnings.removeDuplicates();
    if (warnings.isEmpty())
        return true;
    return Dialogs::confirm(this, QStringLiteral("Осторожно: важные файлы устройства"),
                            warnings.join(QLatin1Char('\n')) + QStringLiteral("\n\nТочно продолжить?"), action, true);
}

void MainWindow::makeDir()
{
    if (JobDialog::busy())
        return;
    FilePanel* p = m_active;
    if (!p->backend() || p->dir().isEmpty())
        return;
    const GuardResult into = guardForWriteInto(p->backend()->kind(), p->dir());
    if (into.level == Guard::Block) {
        Dialogs::error(this, QStringLiteral("Запись сюда запрещена"), into.reason);
        return;
    }
    bool ok = false;
    const QString name = Dialogs::askText(this, QStringLiteral("Новая папка"), QStringLiteral("Название папки:"),
                                          QStringLiteral("Новая папка"), QStringLiteral("Создать"), &ok);
    if (!ok || name.isEmpty())
        return;
    if (name.contains(QLatin1Char('/'))) {
        Dialogs::error(this, QStringLiteral("Недопустимое имя"), QStringLiteral("Имя не может содержать «/»."));
        return;
    }
    if (p->contains(name)) {
        Dialogs::error(this, QStringLiteral("Такое имя уже есть"), name);
        return;
    }
    const BackendPtr b = p->backend();
    const QString path = joinPath(p->dir(), name);
    const JobResult r = JobDialog::run(this, QStringLiteral("Создание папки"), [b, path](JobState&) { b->mkdir(path); });
    SizeIndex::instance().refresh(b, p->dir(), {name});
    p->reload(name);
    reportErrors(this, QStringLiteral("Не удалось создать папку"), r);
}

void MainWindow::renameItem()
{
    if (JobDialog::busy())
        return;
    FilePanel* p = m_active;
    if (!p->hasCurrent())
        return;
    const Entry e = p->currentEntry();
    if (!guardModify(p, {e}, QStringLiteral("Переименовать")))
        return;
    bool ok = false;
    const QString name = Dialogs::askText(this, QStringLiteral("Переименовать"), QStringLiteral("Новое имя:"), e.name,
                                          QStringLiteral("Переименовать"), &ok);
    if (!ok || name.isEmpty() || name == e.name)
        return;
    if (name.contains(QLatin1Char('/'))) {
        Dialogs::error(this, QStringLiteral("Недопустимое имя"), QStringLiteral("Имя не может содержать «/»."));
        return;
    }
    if (p->contains(name) && name.compare(e.name, Qt::CaseInsensitive) != 0) {
        Dialogs::error(this, QStringLiteral("Такое имя уже есть"), name);
        return;
    }
    const BackendPtr b = p->backend();
    const QString dir = p->dir();
    const JobResult r = JobDialog::run(this, QStringLiteral("Переименование"), [b, dir, e, name](JobState&) {
        if (e.parts.isEmpty()) {
            b->rename(joinPath(dir, e.name), joinPath(dir, name));
            return;
        }
        for (const QString& part : e.parts)
            b->rename(joinPath(dir, part), joinPath(dir, name + part.mid(e.name.size())));
    });
    if (e.isDir)
        SizeIndex::instance().rename(b->id(), joinPath(dir, e.name), joinPath(dir, name));
    SizeIndex::instance().refresh(b, dir);
    p->reload(name);
    reportErrors(this, QStringLiteral("Не удалось переименовать"), r);
}

void MainWindow::removeItems()
{
    if (JobDialog::busy())
        return;
    FilePanel* p = m_active;
    const QList<Entry> items = p->selectedEntries();
    if (items.isEmpty() || !p->backend())
        return;
    if (!guardModify(p, items, QStringLiteral("Удалить")))
        return;

    const bool local = p->backend()->isLocal();
    const bool ok = local
        ? Dialogs::confirm(this, QStringLiteral("Переместить в Корзину ") + describe(items) + QLatin1Char('?'),
                           QStringLiteral("Их можно будет вернуть из Корзины."), QStringLiteral("В Корзину"))
        : Dialogs::confirm(this, QStringLiteral("Удалить ") + describe(items) + QStringLiteral(" безвозвратно?"),
                           QStringLiteral("С устройства «%1». Отменить это будет нельзя.").arg(p->backend()->title()),
                           QStringLiteral("Удалить"), true);
    if (!ok)
        return;

    // Pick the row to select afterwards: the next item that survives.
    QSet<QString> removed;
    for (const Entry& e : items)
        removed.insert(e.name);
    QString next;
    const QList<Entry> all = p->entries();
    const QString current = p->hasCurrent() ? p->currentEntry().name : QString();
    int pos = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].name == current)
            pos = i;
    }
    for (int i = pos; i < all.size() && next.isEmpty(); ++i) {
        if (!removed.contains(all[i].name))
            next = all[i].name;
    }
    for (int i = pos; i >= 0 && next.isEmpty(); --i) {
        if (!removed.contains(all[i].name))
            next = all[i].name;
    }

    const BackendPtr b = p->backend();
    const QString dir = p->dir();
    const JobResult r = JobDialog::run(this, local ? QStringLiteral("Перемещение в Корзину") : QStringLiteral("Удаление"),
                                       [b, dir, items, local](JobState& st) { runDelete(st, b, dir, items, local); });
    SizeIndex::instance().refresh(b, dir);
    p->reload(next);
    reportErrors(this, QStringLiteral("Не всё удалось удалить"), r);
}

void MainWindow::newConnection(FilePanel* panel)
{
    ConnectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString id = Devices::instance().addProfile(dlg.profile());
    setActive(panel);
    panel->setLocation(QStringLiteral("ftp:") + id, QStringLiteral("/"));
}

void MainWindow::editConnection(FilePanel* panel)
{
    const QString key = panel->key();
    if (!key.startsWith(QLatin1String("ftp:")))
        return;
    const FtpProfile profile = Devices::instance().profile(key.mid(4));
    if (profile.id.isEmpty())
        return;

    QMenu menu(this);
    QAction* edit = menu.addAction(QStringLiteral("Изменить подключение…"));
    QAction* remove = menu.addAction(QStringLiteral("Удалить подключение"));
    QAction* chosen = menu.exec(QCursor::pos());
    if (chosen == edit) {
        ConnectionDialog dlg(this, profile);
        if (dlg.exec() != QDialog::Accepted)
            return;
        Devices::instance().updateProfile(dlg.profile());
        for (FilePanel* p : {m_left, m_right}) {
            if (p->key() == key)
                p->setLocation(key, QStringLiteral("/"));
        }
    } else if (chosen == remove) {
        if (!Dialogs::confirm(this, QStringLiteral("Удалить подключение «%1»?").arg(profile.name),
                              QStringLiteral("Файлы на устройстве не пострадают, удалится только запись в Folix."),
                              QStringLiteral("Удалить"), true)) {
            return;
        }
        Devices::instance().removeProfile(profile.id);
        for (FilePanel* p : {m_left, m_right}) {
            if (p->key() == key)
                p->setLocation(macKey(QStandardPaths::HomeLocation), QString(), false);
        }
    }
}

void MainWindow::swapPanels()
{
    const QString lk = m_left->key(), ld = m_left->dir();
    const QString rk = m_right->key(), rd = m_right->dir();
    m_left->setLocation(rk, rd, false);
    m_right->setLocation(lk, ld, false);
}

void MainWindow::goPlace(const QString& path)
{
    m_active->setLocation(QStringLiteral("mac:") + path, path);
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowModality(Qt::WindowModal);
    box.setIconPixmap(appIcon().pixmap(96, 96));
    box.setText(QStringLiteral("Folix ") + QApplication::applicationVersion());
    box.setInformativeText(QStringLiteral(
        "Файловый менеджер для Mac, Android, PS Vita, Nintendo Switch и PS3.\n\n"
        "F3 — просмотр\nF5 — копировать в другую панель\nF6 — переместить\nF7 — новая папка\n"
        "F8 или ⌘⌫ — удалить\n⇧F6 или F2 — переименовать\n\n"
        "Размеры папок считаются сами: на Mac при запуске, на устройствах при подключении (⇧⌘R — заново)\n"
        "⌘I или кнопка «Информация» — свойства\n⌘C / ⌘X / ⌘V — копировать, вырезать, вставить (и с Finder тоже)\n"
        "Правый клик — меню действий\nПробел — пересчитать размер папки\nTab — другая панель\n⌫ или ⌘↑ — на уровень выше\n"
        "⌘K — новое FTP-подключение\n⇧⌘. — скрытые файлы\n\n"
        "Перетаскивание между панелями копирует, с зажатой ⌘ — перемещает.\n"
        "F-клавиши на Mac нажимаются вместе с fn, поэтому у каждой есть замена через ⌘."));
    box.addButton(QStringLiteral("OK"), QMessageBox::AcceptRole);
    box.exec();
}

void MainWindow::showInfo()
{
    FilePanel* p = m_active;
    if (!p->backend() || p->dir().isEmpty())
        return;
    const QList<Entry> items = p->view()->selectionModel()->selectedRows().isEmpty() && !p->hasCurrent()
        ? QList<Entry>()
        : p->selectedEntries();
    auto* dlg = new InfoDialog(this, p->backend(), p->dir(), items);
    dlg->show();
}

void MainWindow::copyToClipboard(bool cut)
{
    FilePanel* p = m_active;
    const QList<Entry> items = p->selectedEntries();
    if (items.isEmpty() || !p->backend())
        return;
    m_clip = Clip{p->backend(), p->dir(), items, cut};

    // Mac files also go to the system clipboard so Finder can paste them.
    auto* mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-folix-clip"), QByteArray("1"));
    QStringList names;
    for (const Entry& e : items)
        names << e.name;
    mime->setText(names.join(QLatin1Char('\n')));
    if (p->backend()->isLocal()) {
        QList<QUrl> urls;
        for (const Entry& e : items) {
            for (const QString& n : realNames(e))
                urls << QUrl::fromLocalFile(joinPath(p->dir(), n));
        }
        mime->setUrls(urls);
    }
    QApplication::clipboard()->setMimeData(mime);
    statusBar()->showMessage(QStringLiteral("%1: %2 — выберите папку и нажмите ⌘V")
                                 .arg(cut ? QStringLiteral("Вырезано") : QStringLiteral("Скопировано"))
                                 .arg(items.size() == 1 ? items.first().name : QString::number(items.size()) + QStringLiteral(" объектов")),
                             6000);
}

void MainWindow::paste()
{
    if (JobDialog::busy())
        return;
    FilePanel* target = m_active;
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (m_clip && mime && mime->hasFormat(QStringLiteral("application/x-folix-clip"))) {
        FilePanel* srcPanel = nullptr;
        for (FilePanel* p : {m_left, m_right}) {
            if (p->backend() && p->backend()->id() == m_clip->backend->id() && p->dir() == m_clip->dir)
                srcPanel = p;
        }
        const Clip clip = *m_clip;
        const bool ok = transfer(clip.backend, clip.dir, srcPanel, target, clip.items, clip.cut, false);
        if (ok && clip.cut) {
            m_clip.reset();
            QApplication::clipboard()->clear();
        }
        return;
    }
    // Files copied in Finder
    if (mime && mime->hasUrls()) {
        QList<QUrl> urls;
        for (const QUrl& u : mime->urls()) {
            if (u.isLocalFile())
                urls << u;
        }
        if (!urls.isEmpty())
            transferFromMac(target, urls, false);
    }
}

void MainWindow::showContextMenu(FilePanel* panel, const QPoint& globalPos, bool onItem)
{
    setActive(panel);
    const bool local = panel->backend() && panel->backend()->isLocal();
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    const bool canPaste = (m_clip && mime && mime->hasFormat(QStringLiteral("application/x-folix-clip")))
        || (mime && mime->hasUrls());
    m_actPaste->setEnabled(canPaste);

    QMenu menu(this);
    if (onItem) {
        menu.addAction(m_actOpen);
        if (local && panel->hasCurrent() && panel->currentEntry().isDir
            && panel->currentEntry().name.endsWith(QLatin1String(".app"))) {
            const QString path = joinPath(panel->dir(), panel->currentEntry().name);
            QAction* inside = menu.addAction(QStringLiteral("Показать содержимое пакета"));
            connect(inside, &QAction::triggered, this, [panel, path] { panel->navigate(path); });
        }
        menu.addAction(m_actView);
        menu.addSeparator();
        menu.addAction(m_actCopy);
        menu.addAction(m_actCut);
        menu.addAction(m_actPaste);
        menu.addSeparator();
        menu.addAction(m_actCopyOther);
        menu.addAction(m_actMoveOther);
        menu.addAction(m_actRename);
        menu.addSeparator();
        menu.addAction(m_actDelete);
        menu.addSeparator();
        if (local)
            menu.addAction(m_actFinder);
        menu.addAction(m_actInfo);
    } else {
        menu.addAction(m_actPaste);
        menu.addAction(m_actNewDir);
        menu.addAction(m_actRefresh);
        menu.addSeparator();
        if (local)
            menu.addAction(m_actFinder);
        QAction* info = menu.addAction(QStringLiteral("Свойства этой папки"));
        connect(info, &QAction::triggered, this, [this, panel] {
            (new InfoDialog(this, panel->backend(), panel->dir(), {}))->show();
        });
    }
    menu.exec(globalPos);
    m_actPaste->setEnabled(true);
}

void MainWindow::ejectDrive()
{
    FilePanel* p = m_active;
    const QString dir = p->dir();
    if (!p->backend() || !p->backend()->isLocal() || !dir.startsWith(QLatin1String("/Volumes/"))) {
        Dialogs::info(this, QStringLiteral("Нечего извлекать"), QStringLiteral("Откройте в активной панели внешний диск."));
        return;
    }
    const QString volume = QStringLiteral("/Volumes/") + dir.section(QLatin1Char('/'), 2, 2);
    // Step both panels off the drive so nothing holds it open.
    for (FilePanel* panel : {m_left, m_right}) {
        if (panel->dir() == volume || panel->dir().startsWith(volume + QLatin1Char('/')))
            panel->setLocation(macKey(QStandardPaths::HomeLocation), QString(), false);
    }
    const JobResult r = JobDialog::run(this, QStringLiteral("Извлечение диска"), [volume](JobState& st) {
        st.setCurrent(volume);
        const auto res = Tools::run(QStringLiteral("/usr/sbin/diskutil"), {QStringLiteral("eject"), volume}, 60000);
        if (res.code != 0)
            throw FsError(QString::fromUtf8(res.err.isEmpty() ? res.out : res.err).trimmed());
    });
    if (r.ok())
        statusBar()->showMessage(QStringLiteral("Диск можно отключать"), 6000);
    else
        reportErrors(this, QStringLiteral("Не удалось извлечь диск"), r);
}

void MainWindow::startLocalServer()
{
    // FolixAgent sends "open\t<path>" here when a drive is connected.
    m_server = new QLocalServer(this);
    QLocalServer::removeServer(AutoOpen::socketPath());
    if (!m_server->listen(AutoOpen::socketPath()))
        return;
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* sock = m_server->nextPendingConnection()) {
            connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
                while (sock->canReadLine()) {
                    const QString line = QString::fromUtf8(sock->readLine()).trimmed();
                    if (line.startsWith(QLatin1String("open\t")))
                        openExternal(line.mid(5));
                }
            });
            connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });
}

void MainWindow::openExternal(const QString& path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;
    if (!m_ready) {
        m_pendingOpen = path;
        return;
    }
    m_right->setLocation(QStringLiteral("mac:") + path, path, false);
    setActive(m_right);
    m_right->view()->setFocus();
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
}

void MainWindow::restoreState()
{
    QSettings s;
    if (!restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray()))
        resize(1280, 800);
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("panels")))
        splitter->restoreState(s.value(QStringLiteral("splitter")).toByteArray());

    const bool hidden = s.value(QStringLiteral("showHidden"), false).toBool();
    m_hiddenAction->setChecked(hidden);
    m_left->setShowHidden(hidden);
    m_right->setShowHidden(hidden);
}

void MainWindow::restorePanels()
{
    QSettings s;
    auto restorePanel = [&](FilePanel* p, const QString& prefix, const QString& defaultKey) {
        p->restoreHeader(s.value(prefix + QStringLiteral("/header")).toByteArray());
        QString key = s.value(prefix + QStringLiteral("/key"), defaultKey).toString();
        QString dir = s.value(prefix + QStringLiteral("/dir")).toString();
        // Devices may be switched off now, so a panel always starts on the Mac.
        if (!key.startsWith(QLatin1String("mac:"))) {
            key = defaultKey;
            dir.clear();
        }
        if (dir.isEmpty() || !QFileInfo(dir).isDir())
            dir = Devices::instance().startPath(key);
        p->setLocation(key, dir, false);
    };
    restorePanel(m_left, QStringLiteral("left"), macKey(QStandardPaths::HomeLocation));
    restorePanel(m_right, QStringLiteral("right"), macKey(QStandardPaths::DownloadLocation));

    setActive(m_left);
    m_left->view()->setFocus();
}

void MainWindow::saveState()
{
    QSettings s;
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    if (auto* splitter = findChild<QSplitter*>(QStringLiteral("panels")))
        s.setValue(QStringLiteral("splitter"), splitter->saveState());
    auto savePanel = [&](FilePanel* p, const QString& prefix) {
        s.setValue(prefix + QStringLiteral("/key"), p->key());
        s.setValue(prefix + QStringLiteral("/dir"), p->dir());
        s.setValue(prefix + QStringLiteral("/header"), p->saveHeader());
    };
    savePanel(m_left, QStringLiteral("left"));
    savePanel(m_right, QStringLiteral("right"));
    s.setValue(QStringLiteral("showHidden"), m_hiddenAction->isChecked());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveState();
    if (m_server)
        m_server->close();
    SizeIndex::instance().shutdown();
    PreviewPopup::instance()->hidePopup();
    event->accept();
    qApp->quit();
}
