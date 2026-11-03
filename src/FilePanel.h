#pragma once

#include "Backend.h"

#include <QComboBox>
#include <QTimer>
#include <QTreeView>
#include <QUrl>
#include <QWidget>

class FileModel;
class QLabel;
class QLineEdit;
class QThreadPool;
class QToolButton;

class DeviceCombo : public QComboBox {
    Q_OBJECT
public:
    using QComboBox::QComboBox;
    void showPopup() override;
signals:
    void aboutToShowPopup();
};

class PanelView : public QTreeView {
    Q_OBJECT
public:
    using QTreeView::QTreeView;

signals:
    void openRequested();
    void upRequested();
    void switchRequested();
    void sizeRequested();
    void focused();
    void dropped(QObject* sourceView, const QList<QUrl>& urls, bool move);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool acceptable(const QMimeData* mime) const;
};

class FilePanel : public QWidget {
    Q_OBJECT
public:
    explicit FilePanel(QWidget* parent = nullptr);

    void setLocation(const QString& key, const QString& dir, bool interactive = true);
    void navigate(const QString& dir, const QString& selectName = {}, bool interactive = true);
    void reload(const QString& selectName = {});
    void goUp();
    void openCurrent();

    BackendPtr backend() const { return m_backend; }
    QString key() const { return m_key; }
    QString dir() const { return m_dir; }
    QList<Entry> selectedEntries() const;
    QList<Entry> entries() const;
    bool hasCurrent() const;
    Entry currentEntry() const;
    bool contains(const QString& name) const;
    QTreeView* view() const { return m_view; }

    void setActive(bool active);
    void setShowHidden(bool show);
    void calcSelectedSizes();
    void calcAllSizes();
    void refreshDevices();
    QByteArray saveHeader() const;
    void restoreHeader(const QByteArray& state);

signals:
    void activated(FilePanel* panel);
    void openFile(FilePanel* panel, const Entry& e);
    void openArchive(FilePanel* panel, const Entry& e);
    void switchRequested();
    void newConnectionRequested(FilePanel* panel);
    void editConnectionRequested(FilePanel* panel);
    void dropRequested(FilePanel* target, QObject* sourceView, const QList<QUrl>& urls, bool move);
    void locationChanged();
    void contextMenuRequested(FilePanel* panel, const QPoint& globalPos, bool onItem);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void deviceChosen(int index);
    void updateStatus();
    void updateFreeSpace();
    void calcSizes(const QList<Entry>& dirs);
    void showHover();
    QHash<QString, qint64> indexSizes(const QList<Entry>& entries) const;
    void hideHover();
    void applyTint();

    BackendPtr m_backend;
    QString m_key;
    QString m_dir;
    quint64 m_gen = 0;
    qint64 m_free = -1;
    std::shared_ptr<std::atomic_bool> m_sizeCancel;

    QFrame* m_accent;
    DeviceCombo* m_combo;
    QToolButton* m_edit;
    QToolButton* m_refresh;
    QLineEdit* m_path;
    PanelView* m_view;
    FileModel* m_model;
    QLabel* m_status;
    QThreadPool* m_pool;

    QTimer m_hoverTimer;
    int m_hoverRow = -1;
};
