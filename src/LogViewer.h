#pragma once

#include <QToolButton>
#include <QWidget>

class QPlainTextEdit;

// Small icon in the bottom-right corner; a red dot means new errors.
class LogButton : public QToolButton {
    Q_OBJECT
public:
    explicit LogButton(QWidget* parent = nullptr);

private:
    void updateIcon();
};

class LogViewer : public QWidget {
    Q_OBJECT
public:
    static void showWindow();

private:
    LogViewer();
    void reload();

    QPlainTextEdit* m_text;
};
