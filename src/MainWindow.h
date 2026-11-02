#pragma once

#include "Backend.h"

#include <QMainWindow>
#include <QUrl>

#include <optional>

class FilePanel;
class QAction;
class QLocalServer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    // Shows a folder (e.g. a freshly connected drive) in the right panel and brings the window up.
    void openExternal(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    QWidget* buildFunctionBar();
    void setActive(FilePanel* panel);
    FilePanel* other(FilePanel* panel) const;

    void view();
    void copyOrMove(bool move);
    bool transfer(const BackendPtr& srcBackend, const QString& srcDir, FilePanel* srcPanel, FilePanel* dst,
                  QList<Entry> items, bool move, bool ask);
    void transferFromMac(FilePanel* dst, const QList<QUrl>& urls, bool move);
    void makeDir();
    void renameItem();
    void removeItems();
    void newConnection(FilePanel* panel);
    void editConnection(FilePanel* panel);
    void handleDrop(FilePanel* target, QObject* sourceView, const QList<QUrl>& urls, bool move);
    void openFile(FilePanel* panel, const Entry& e);
    void about();
    void swapPanels();
    void showInfo();
    void copyToClipboard(bool cut);
    void paste();
    void showContextMenu(FilePanel* panel, const QPoint& globalPos, bool onItem);
    void buildDiskMenu(QMenu* menu);
    void ejectDrive();
    void startLocalServer();
    void goPlace(const QString& path);
    bool guardModify(FilePanel* panel, const QList<Entry>& items, const QString& action);

    void restoreState();
    void restorePanels();
    void saveState();

    FilePanel* m_left;
    FilePanel* m_right;
    FilePanel* m_active;
    QAction* m_hiddenAction = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actView = nullptr;
    QAction* m_actCopy = nullptr;
    QAction* m_actCut = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actCopyOther = nullptr;
    QAction* m_actMoveOther = nullptr;
    QAction* m_actRename = nullptr;
    QAction* m_actNewDir = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actInfo = nullptr;
    QAction* m_actRefresh = nullptr;
    QAction* m_actFinder = nullptr;
    QLocalServer* m_server = nullptr;
    bool m_ready = false;        // panels restored after the start-up permission check
    QString m_pendingOpen;       // drive to show once ready

    struct Clip {
        BackendPtr backend;
        QString dir;
        QList<Entry> items;
        bool cut = false;
    };
    std::optional<Clip> m_clip;
};
