#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QMutex>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

class QLabel;
class QProgressBar;
class QPushButton;

// Shared between the worker thread and the progress sheet.
class JobState {
public:
    std::atomic_bool cancel{false};

    void setCurrent(const QString& text);
    void setProgress(qint64 done, qint64 total);
    void addError(const QString& error);

    struct Snapshot {
        QString current;
        qint64 done = 0;
        qint64 total = 0;
        QStringList errors;
    };
    Snapshot snapshot() const;

private:
    mutable QMutex m_mutex;
    QString m_current;
    qint64 m_done = 0;
    qint64 m_total = 0;
    QStringList m_errors;
};

struct JobResult {
    bool cancelled = false;
    QStringList errors;
    bool ok() const { return !cancelled && errors.isEmpty(); }
};

class JobDialog : public QDialog {
    Q_OBJECT
public:
    // Runs `work` on a background thread. A progress sheet appears if it takes longer than a moment.
    static JobResult run(QWidget* parent, const QString& title, const std::function<void(JobState&)>& work);
    static bool busy();

protected:
    void reject() override;

private:
    JobDialog(QWidget* parent, const QString& title, std::shared_ptr<JobState> state);
    void refresh();

    std::shared_ptr<JobState> m_state;
    QLabel* m_current;
    QLabel* m_detail;
    QProgressBar* m_bar;
    QPushButton* m_cancel;
    QElapsedTimer m_clock;
    qint64 m_lastDone = 0;
    qint64 m_lastMs = 0;
    double m_speed = 0;
};
