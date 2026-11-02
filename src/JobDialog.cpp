#include "JobDialog.h"

#include "Backend.h"
#include "Tools.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {
bool g_busy = false;
}

void JobState::setCurrent(const QString& text)
{
    QMutexLocker l(&m_mutex);
    m_current = text;
}

void JobState::setProgress(qint64 done, qint64 total)
{
    QMutexLocker l(&m_mutex);
    m_done = done;
    m_total = total;
}

void JobState::addError(const QString& error)
{
    QMutexLocker l(&m_mutex);
    m_errors.append(error);
}

JobState::Snapshot JobState::snapshot() const
{
    QMutexLocker l(&m_mutex);
    return {m_current, m_done, m_total, m_errors};
}

bool JobDialog::busy()
{
    return g_busy;
}

JobResult JobDialog::run(QWidget* parent, const QString& title, const std::function<void(JobState&)>& work)
{
    auto state = std::make_shared<JobState>();
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    g_busy = true;

    QFutureWatcher<void> watcher;
    QEventLoop loop;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([state, cancelled, work] {
        try {
            work(*state);
        } catch (const Cancelled&) {
            *cancelled = true;
        } catch (const FsError& e) {
            state->addError(e.message());
        } catch (const std::exception& e) {
            state->addError(QString::fromUtf8(e.what()));
        }
        if (state->cancel)
            *cancelled = true;
    }));

    JobDialog dlg(parent, title, state);
    QTimer::singleShot(350, &dlg, [&] {
        if (!watcher.isFinished())
            dlg.open();
    });
    if (!watcher.isFinished())
        loop.exec();
    dlg.hide();
    g_busy = false;

    JobResult r;
    r.cancelled = *cancelled;
    r.errors = state->snapshot().errors;
    return r;
}

JobDialog::JobDialog(QWidget* parent, const QString& title, std::shared_ptr<JobState> state)
    : QDialog(parent), m_state(std::move(state))
{
    setWindowModality(Qt::WindowModal);
    setWindowTitle(title);
    setMinimumWidth(480);

    auto* heading = new QLabel(title, this);
    QFont f = heading->font();
    f.setBold(true);
    f.setPointSizeF(f.pointSizeF() + 1);
    heading->setFont(f);

    m_current = new QLabel(this);
    m_current->setTextFormat(Qt::PlainText);
    m_current->setMinimumWidth(440);
    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(false);
    m_bar->setRange(0, 0);
    m_detail = new QLabel(this);
    m_detail->setForegroundRole(QPalette::PlaceholderText);
    m_cancel = new QPushButton(QStringLiteral("Отмена"), this);
    connect(m_cancel, &QPushButton::clicked, this, &JobDialog::reject);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_cancel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(8);
    layout->addWidget(heading);
    layout->addWidget(m_current);
    layout->addWidget(m_bar);
    layout->addWidget(m_detail);
    layout->addLayout(buttons);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &JobDialog::refresh);
    timer->start(120);
    m_clock.start();
}

void JobDialog::refresh()
{
    const auto s = m_state->snapshot();
    m_current->setText(m_current->fontMetrics().elidedText(s.current, Qt::ElideMiddle, m_current->width()));
    if (s.total <= 0) {
        m_bar->setRange(0, 0);
        m_detail->setText(s.done > 0 ? Tools::humanSize(s.done) : QString());
        return;
    }
    m_bar->setRange(0, 1000);
    m_bar->setValue(int(std::clamp<qint64>(s.done * 1000 / s.total, 0, 1000)));

    const qint64 now = m_clock.elapsed();
    if (now - m_lastMs >= 500) {
        const double inst = double(s.done - m_lastDone) * 1000.0 / double(qMax<qint64>(1, now - m_lastMs));
        m_speed = m_speed <= 0 ? inst : m_speed * 0.7 + inst * 0.3;
        m_lastDone = s.done;
        m_lastMs = now;
    }
    QString text = Tools::humanSize(s.done) + QStringLiteral(" из ") + Tools::humanSize(s.total);
    if (m_speed > 1) {
        text += QStringLiteral(" · ") + Tools::humanSize(qint64(m_speed)) + QStringLiteral("/с");
        const qint64 left = qint64(double(s.total - s.done) / m_speed);
        if (left >= 60)
            text += QStringLiteral(" · осталось ~%1 мин").arg((left + 59) / 60);
        else if (left > 0)
            text += QStringLiteral(" · осталось ~%1 с").arg(left);
    }
    m_detail->setText(text);
}

void JobDialog::reject()
{
    m_state->cancel = true;
    m_cancel->setEnabled(false);
    m_cancel->setText(QStringLiteral("Отменяю…"));
}
