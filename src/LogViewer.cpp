#include "LogViewer.h"

#include "Dialogs.h"
#include "Log.h"
#include "Platform.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

QIcon logIcon(const QColor& ink, bool alert)
{
    QIcon icon;
    for (int px : {16, 32, 48}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(px / 16.0, px / 16.0);
        p.setPen(QPen(ink, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(2.5, 1.5, 11, 13), 2, 2);
        p.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap));
        for (qreal y : {5.0, 8.0, 11.0})
            p.drawLine(QPointF(5, y), QPointF(y == 11.0 ? 8.5 : 11, y));
        if (alert) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 59, 48));
            p.drawEllipse(QPointF(12.8, 3.2), 3.0, 3.0);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

LogButton::LogButton(QWidget* parent) : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(16, 16));
    setToolTip(QStringLiteral("Логи"));
    connect(this, &QToolButton::clicked, this, [] { LogViewer::showWindow(); });
    connect(&Log::instance(), &Log::added, this, &LogButton::updateIcon, Qt::QueuedConnection);
    updateIcon();
}

void LogButton::updateIcon()
{
    const int unseen = Log::instance().unseenErrors();
    setIcon(logIcon(palette().color(QPalette::ButtonText), unseen > 0));
    setToolTip(unseen > 0 ? QStringLiteral("Логи — новых ошибок: %1").arg(unseen) : QStringLiteral("Логи"));
}

void LogViewer::showWindow()
{
    static QPointer<LogViewer> window;
    if (!window)
        window = new LogViewer;
    window->reload();
    window->show();
    window->raise();
    window->activateWindow();
    Log::instance().markSeen();
}

LogViewer::LogViewer()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("Логи Folix"));
    resize(900, 560);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setPlaceholderText(QStringLiteral("Ошибок пока не было."));

    auto* copy = new QPushButton(QStringLiteral("Скопировать всё"), this);
    auto* finder = new QPushButton(QStringLiteral("Показать в ") + Platform::fileManagerName(), this);
    auto* clear = new QPushButton(QStringLiteral("Очистить"), this);
    for (QPushButton* b : {copy, finder, clear}) {
        b->setAutoDefault(false);
        b->setMinimumWidth(b->sizeHint().width() + 8);
    }
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_text->toPlainText()); });
    connect(finder, &QPushButton::clicked, this, [] { Platform::reveal(Log::filePath()); });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (Dialogs::confirm(this, QStringLiteral("Очистить логи?"), QStringLiteral("Все записи об ошибках будут удалены."),
                             QStringLiteral("Очистить"), true)) {
            Log::clear();
            reload();
        }
    });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(clear);
    buttons->addStretch();
    buttons->addWidget(finder);
    buttons->addWidget(copy);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 10);
    layout->addWidget(m_text, 1);
    auto* bottom = new QWidget(this);
    bottom->setLayout(buttons);
    buttons->setContentsMargins(12, 4, 12, 0);
    layout->addWidget(bottom);

    // Live: new entries appear while the window is open.
    connect(&Log::instance(), &Log::added, this, [this] {
        reload();
        Log::instance().markSeen();
    }, Qt::QueuedConnection);
}

void LogViewer::reload()
{
    const QString text = Log::read();
    if (text == m_text->toPlainText())
        return;
    m_text->setPlainText(text);
    m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
}
