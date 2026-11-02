#pragma once

#include "Backend.h"

#include <QDialog>
#include <QTimer>

class QFormLayout;
class QLabel;

// "Свойства" window, like Get Info in Finder. Sizes and counts are computed in the background.
class InfoDialog : public QDialog {
    Q_OBJECT
public:
    // `items` empty: information about the folder `dir` itself.
    InfoDialog(QWidget* parent, const BackendPtr& b, const QString& dir, const QList<Entry>& items);
    ~InfoDialog() override;

private:
    QLabel* addRow(const QString& title, const QString& value);
    QString pathOf(const Entry& e) const { return m_self ? m_dir : joinPath(m_dir, e.name); }
    void startCounting();
    void updateCounting();

    BackendPtr m_backend;
    QString m_dir;
    QList<Entry> m_items;
    bool m_self = false;   // info about the open folder itself
    QFormLayout* m_form;
    QLabel* m_size = nullptr;
    QLabel* m_contents = nullptr;
    QLabel* m_headerSize = nullptr;
    qint64 m_fileBytes = 0;    // sizes of selected files (known right away)
    int m_selectedFiles = 0;
    int m_selectedDirs = 0;
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic<qint64>> m_seenFiles = std::make_shared<std::atomic<qint64>>(0);
    QTimer m_tick;
};
