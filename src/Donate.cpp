#include "Donate.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct Card {
    const char* network;   // "visa" or "mastercard"
    const char* number;    // as shown, grouped by four
};

const Card kCards[] = {
    {"visa", "4916 9903 0007 3175"},
    {"mastercard", "5321 5400 1305 0170"},
};
const char* kHolder = "Mavlonov Umidjon";

QIcon coffeeIcon(const QColor& ink)
{
    QIcon icon;
    for (int px : {16, 32, 48}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(px / 16.0, px / 16.0);
        p.setPen(QPen(ink, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        // cup
        QPainterPath cup;
        cup.moveTo(2.5, 6.5);
        cup.lineTo(11.5, 6.5);
        cup.lineTo(10.8, 11.5);
        cup.quadTo(10.5, 13.2, 8.8, 13.2);
        cup.lineTo(5.2, 13.2);
        cup.quadTo(3.5, 13.2, 3.2, 11.5);
        cup.closeSubpath();
        p.drawPath(cup);
        // handle
        p.drawArc(QRectF(10.2, 7.4, 4.2, 3.8), -100 * 16, 200 * 16);
        // steam
        for (qreal x : {5.2, 8.0}) {
            QPainterPath s;
            s.moveTo(x, 4.8);
            s.cubicTo(x - 0.9, 3.8, x + 0.9, 2.8, x, 1.6);
            p.drawPath(s);
        }
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

// A white cat sitting with its tail around its paws.
class CatPicture : public QWidget {
public:
    explicit CatPicture(QWidget* parent) : QWidget(parent) { setFixedSize(190, 170); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(width() / 2.0, 0);
        const QColor fur(255, 255, 255);
        const QColor shade(226, 228, 236);
        const QColor line(150, 154, 168);
        const QColor pink(255, 168, 182);

        // soft shadow on the floor
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 28));
        p.drawEllipse(QPointF(0, 160), 62, 7);

        // tail
        QPainterPath tail;
        tail.moveTo(38, 150);
        tail.cubicTo(78, 150, 84, 112, 64, 96);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(line, 15.5, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(tail);
        p.setPen(QPen(fur, 13, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(tail);

        // body
        QLinearGradient bodyGrad(0, 80, 0, 160);
        bodyGrad.setColorAt(0, fur);
        bodyGrad.setColorAt(1, shade);
        p.setPen(QPen(line, 1.6));
        p.setBrush(bodyGrad);
        QPainterPath body;
        body.moveTo(-34, 154);
        body.cubicTo(-52, 150, -50, 96, -22, 84);
        body.lineTo(22, 84);
        body.cubicTo(50, 96, 52, 150, 34, 154);
        body.closeSubpath();
        p.drawPath(body);

        // front paws
        p.setBrush(fur);
        p.drawEllipse(QPointF(-14, 152), 11, 7);
        p.drawEllipse(QPointF(14, 152), 11, 7);

        // ears
        auto ear = [&](qreal dir) {
            QPainterPath e;
            e.moveTo(dir * 18, 34);
            e.lineTo(dir * 40, 6);
            e.lineTo(dir * 44, 46);
            e.closeSubpath();
            p.setPen(QPen(line, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(fur);
            p.drawPath(e);
            QPainterPath in;
            in.moveTo(dir * 24, 34);
            in.lineTo(dir * 38, 16);
            in.lineTo(dir * 40, 40);
            in.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(pink);
            p.drawPath(in);
        };
        ear(-1);
        ear(1);

        // head
        p.setPen(QPen(line, 1.6));
        p.setBrush(fur);
        p.drawEllipse(QPointF(0, 58), 46, 38);

        // eyes, closed and happy
        p.setPen(QPen(QColor(60, 62, 72), 2.4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(-26, 48, 16, 12), 20 * 16, 140 * 16);
        p.drawArc(QRectF(10, 48, 16, 12), 20 * 16, 140 * 16);

        // cheeks
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 170, 185, 110));
        p.drawEllipse(QPointF(-28, 70), 7, 4.5);
        p.drawEllipse(QPointF(28, 70), 7, 4.5);

        // nose and mouth
        QPainterPath nose;
        nose.moveTo(-4, 64);
        nose.lineTo(4, 64);
        nose.lineTo(0, 69);
        nose.closeSubpath();
        p.setBrush(pink);
        p.drawPath(nose);
        p.setPen(QPen(QColor(90, 92, 104), 1.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(-7, 66, 7, 6), 180 * 16, 180 * 16);
        p.drawArc(QRectF(0, 66, 7, 6), 180 * 16, 180 * 16);

        // whiskers
        p.setPen(QPen(line, 1.1, Qt::SolidLine, Qt::RoundCap));
        for (int side : {-1, 1}) {
            p.drawLine(QPointF(side * 20, 66), QPointF(side * 50, 60));
            p.drawLine(QPointF(side * 20, 70), QPointF(side * 51, 70));
            p.drawLine(QPointF(side * 20, 74), QPointF(side * 48, 80));
        }
    }
};

QWidget* networkBadge(const QString& network, QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setFixedSize(58, 34);
    QPixmap pm(58 * 2, 34 * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (network == QLatin1String("visa")) {
        p.setBrush(QColor(26, 31, 113));
        p.drawRoundedRect(QRectF(1, 1, 56, 32), 6, 6);
        QFont f;
        f.setBold(true);
        f.setItalic(true);
        f.setPixelSize(15);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(1, 1, 56, 32), Qt::AlignCenter, QStringLiteral("VISA"));
    } else {
        p.setBrush(QColor(28, 28, 32));
        p.drawRoundedRect(QRectF(1, 1, 56, 32), 6, 6);
        p.setBrush(QColor(235, 0, 27));
        p.drawEllipse(QPointF(23, 17), 10, 10);
        p.setBrush(QColor(247, 158, 27, 230));
        p.drawEllipse(QPointF(35, 17), 10, 10);
    }
    p.end();
    l->setPixmap(pm);
    return l;
}

QPushButton* copyButton(const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(QStringLiteral("Скопировать"), parent);
    b->setAutoDefault(false);
    b->setMinimumWidth(b->sizeHint().width() + 16);
    QObject::connect(b, &QPushButton::clicked, b, [b, text] {
        QString digits = text;
        digits.remove(QLatin1Char(' '));
        QApplication::clipboard()->setText(digits);
        b->setText(QStringLiteral("Скопировано ✓"));
        QTimer::singleShot(1500, b, [b] { b->setText(QStringLiteral("Скопировать")); });
    });
    return b;
}

} // namespace

DonateButton::DonateButton(QWidget* parent) : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setIconSize(QSize(18, 18));
    setToolTip(QStringLiteral("Донат — угостить автора кофе"));
    setIcon(coffeeIcon(QColor(176, 110, 60)));
    connect(this, &QToolButton::clicked, this, [this] { Donate::showWindow(window()); });
}

namespace Donate {

void showWindow(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setWindowTitle(QStringLiteral("Донат"));
    dlg.setMinimumWidth(460);

    auto* title = new QLabel(QStringLiteral("Угостить автора кофе ☕"), &dlg);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 3);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);

    auto* text = new QLabel(QStringLiteral("Folix бесплатный. Если он вам пригодился, можно поддержать автора — "
                                           "на кофе и новые возможности."), &dlg);
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignCenter);

    auto* optional = new QLabel(QStringLiteral("Donations are completely optional — Folix stays free for everyone."), &dlg);
    optional->setWordWrap(true);
    optional->setAlignment(Qt::AlignCenter);
    optional->setForegroundRole(QPalette::PlaceholderText);
    QFont of = optional->font();
    of.setItalic(true);
    optional->setFont(of);

    auto* cards = new QGridLayout;
    cards->setHorizontalSpacing(14);
    cards->setVerticalSpacing(10);
    QFont mono = QFont(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(text->font().pointSizeF() + 2);
    int row = 0;
    for (const Card& c : kCards) {
        auto* number = new QLabel(QString::fromLatin1(c.number), &dlg);
        number->setFont(mono);
        number->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cards->addWidget(networkBadge(QString::fromLatin1(c.network), &dlg), row, 0);
        cards->addWidget(number, row, 1);
        cards->addWidget(copyButton(QString::fromLatin1(c.number), &dlg), row, 2);
        ++row;
    }
    auto* holderTitle = new QLabel(QStringLiteral("Получатель"), &dlg);
    holderTitle->setForegroundRole(QPalette::PlaceholderText);
    auto* holder = new QLabel(QString::fromLatin1(kHolder), &dlg);
    holder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont hf = holder->font();
    hf.setBold(true);
    holder->setFont(hf);
    cards->addWidget(holderTitle, row, 0);
    cards->addWidget(holder, row, 1);
    cards->setColumnStretch(1, 1);

    auto* close = new QPushButton(QStringLiteral("Закрыть"), &dlg);
    close->setDefault(true);
    QObject::connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(10);
    layout->addWidget(new CatPicture(&dlg), 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(text);
    layout->addWidget(optional);
    layout->addSpacing(8);
    layout->addLayout(cards);
    layout->addSpacing(10);
    layout->addLayout(buttons);
    dlg.exec();
}

} // namespace Donate
