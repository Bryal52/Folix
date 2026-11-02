#include "FullDiskAccess.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString bundlePath()
{
    return QDir(QCoreApplication::applicationDirPath() + QStringLiteral("/../..")).canonicalPath();
}

} // namespace

namespace FullDiskAccess {

bool granted()
{
    // The user's TCC database can only be opened with Full Disk Access; failing here shows no prompt.
    QFile f(QDir::homePath() + QStringLiteral("/Library/Application Support/com.apple.TCC/TCC.db"));
    return f.open(QIODevice::ReadOnly);
}

void openSettings()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles")));
}

void ask(QWidget* parent, bool force)
{
    QSettings settings;
    if (granted() || (!force && settings.value(QStringLiteral("fullDiskAccess/dontAsk")).toBool()))
        return;

    QDialog dlg(parent);
    dlg.setWindowModality(Qt::WindowModal);
    dlg.setWindowTitle(QStringLiteral("Доступ к папкам"));
    dlg.setMinimumWidth(560);

    auto* icon = new QLabel(&dlg);
    icon->setPixmap(QIcon(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/Folix.icns")).pixmap(64, 64));

    auto* title = new QLabel(QStringLiteral("Одно разрешение вместо десятка"), &dlg);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 2);
    title->setFont(tf);

    auto* text = new QLabel(QStringLiteral(
        "macOS спрашивает разрешение отдельно для Рабочего стола, Документов, Загрузок, "
        "флешек и сетевых дисков. Чтобы не нажимать «Разрешить» много раз, дайте Folix "
        "<b>«Полный доступ к диску»</b>: это одно разрешение на все папки.<br><br>"
        "1. Нажмите <b>«Открыть настройки»</b>.<br>"
        "2. Включите переключатель у <b>Folix</b>. Если Folix нет в списке, нажмите <b>+</b> "
        "внизу и выберите его в папке «Программы».<br>"
        "3. Вернитесь сюда: Folix сам заметит, что доступ появился."), &dlg);
    text->setWordWrap(true);
    text->setTextFormat(Qt::RichText);

    auto* status = new QLabel(&dlg);
    status->setForegroundRole(QPalette::PlaceholderText);

    auto* dontAsk = new QCheckBox(QStringLiteral("Больше не спрашивать"), &dlg);

    auto* later = new QPushButton(QStringLiteral("Позже"), &dlg);
    auto* restart = new QPushButton(QStringLiteral("Перезапустить Folix"), &dlg);
    restart->setVisible(false);
    auto* open = new QPushButton(QStringLiteral("Открыть настройки"), &dlg);
    open->setDefault(true);

    // Buttons never get narrower than their text.
    for (QPushButton* b : {later, restart, open})
        b->setMinimumWidth(b->sizeHint().width() + 8);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(later);
    buttons->addWidget(restart);
    buttons->addWidget(open);

    auto* right = new QVBoxLayout;
    right->addWidget(title);
    right->addWidget(text);
    right->addWidget(status);
    auto* top = new QHBoxLayout;
    top->addWidget(icon, 0, Qt::AlignTop);
    top->addSpacing(10);
    top->addLayout(right, 1);

    auto* layout = new QVBoxLayout(&dlg);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(14);
    layout->addLayout(top);
    layout->addWidget(dontAsk);
    layout->addLayout(buttons);

    QTimer poll;
    poll.setInterval(1000);
    QObject::connect(&poll, &QTimer::timeout, &dlg, [&] {
        if (granted()) {
            poll.stop();
            status->setText(QStringLiteral("✓ Доступ получен. Спасибо!"));
            QTimer::singleShot(900, &dlg, &QDialog::accept);
        }
    });
    QObject::connect(open, &QPushButton::clicked, &dlg, [&] {
        openSettings();
        status->setText(QStringLiteral("Жду, пока вы включите доступ… Если переключатель уже включён, "
                                       "а Folix этого не видит, перезапустите его."));
        restart->setVisible(true);
        poll.start();
    });
    QObject::connect(restart, &QPushButton::clicked, &dlg, [] {
        QProcess::startDetached(QStringLiteral("/bin/sh"),
                                {QStringLiteral("-c"), QStringLiteral("sleep 1; open \"$0\""), bundlePath()});
        QCoreApplication::quit();
    });
    QObject::connect(later, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
    poll.stop();
    if (dontAsk->isChecked())
        settings.setValue(QStringLiteral("fullDiskAccess/dontAsk"), true);
}

} // namespace FullDiskAccess
