#include "ConnectionDialog.h"

#include "Devices.h"
#include "Dialogs.h"
#include "FileTypes.h"
#include "JobDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

struct KindPreset {
    DeviceKind kind;
    int port;
    const char* hint;
};

const KindPreset kPresets[] = {
    {DeviceKind::Vita, 1337,
     "Откройте VitaShell и нажмите SELECT. На экране появятся IP-адрес и порт (обычно 1337)."},
    {DeviceKind::Switch, 5000,
     "Запустите ftpd (порт 5000) или DBI → «Run FTP server». IP-адрес показан на экране консоли."},
    {DeviceKind::PS3, 21,
     "webMAN MOD запускает FTP сам (порт 21). IP-адрес: Настройки → Настройки сети → "
     "Список настроек и состояний соединения. Лучше подключать PS3 к роутеру кабелем."},
    {DeviceKind::Android, 2221,
     "Запустите на телефоне приложение FTP-сервера. IP-адрес и порт показаны в приложении."},
    {DeviceKind::Other, 21, "Любой FTP-сервер в вашей сети."},
};

} // namespace

ConnectionDialog::ConnectionDialog(QWidget* parent, const FtpProfile& profile)
    : QDialog(parent), m_original(profile)
{
    setWindowModality(Qt::WindowModal);
    setWindowTitle(profile.id.isEmpty() ? QStringLiteral("Новое подключение") : QStringLiteral("Изменить подключение"));
    setMinimumWidth(460);

    m_kind = new QComboBox(this);
    for (const KindPreset& k : kPresets)
        m_kind->addItem(deviceIcon(k.kind), Devices::kindName(k.kind), int(k.kind));
    m_name = new QLineEdit(this);
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("например, 192.168.1.50"));
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(QStringLiteral("anonymous"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("не нужен для консолей"));
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setForegroundRole(QPalette::PlaceholderText);

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Устройство:"), m_kind);
    form->addRow(QStringLiteral("Название:"), m_name);
    form->addRow(QStringLiteral("IP-адрес:"), m_host);
    form->addRow(QStringLiteral("Порт:"), m_port);
    form->addRow(QStringLiteral("Логин:"), m_user);
    form->addRow(QStringLiteral("Пароль:"), m_password);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* testBtn = buttons->addButton(QStringLiteral("Проверить"), QDialogButtonBox::ActionRole);
    buttons->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    QPushButton* save = buttons->addButton(QStringLiteral("Сохранить"), QDialogButtonBox::AcceptRole);
    save->setDefault(true);
    connect(testBtn, &QPushButton::clicked, this, &ConnectionDialog::test);
    connect(buttons, &QDialogButtonBox::accepted, this, &ConnectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ConnectionDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->addLayout(form);
    layout->addWidget(m_hint);
    layout->addSpacing(6);
    layout->addWidget(buttons);

    connect(m_name, &QLineEdit::textEdited, this, [this] { m_nameTouched = true; });
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ConnectionDialog::kindChanged);

    if (profile.id.isEmpty()) {
        m_kind->setCurrentIndex(0);
        kindChanged();
    } else {
        m_nameTouched = true;
        m_kind->setCurrentIndex(m_kind->findData(int(profile.kind)));
        m_name->setText(profile.name);
        m_host->setText(profile.host);
        m_port->setValue(profile.port);
        m_user->setText(profile.user == QLatin1String("anonymous") ? QString() : profile.user);
        m_password->setText(profile.password);
        m_hint->setText(QString::fromUtf8(kPresets[qMax(0, m_kind->currentIndex())].hint));
    }
}

void ConnectionDialog::kindChanged()
{
    const int i = qMax(0, m_kind->currentIndex());
    const KindPreset& k = kPresets[i];
    m_port->setValue(k.port);
    m_hint->setText(QString::fromUtf8(k.hint));
    if (!m_nameTouched)
        m_name->setText(Devices::kindName(k.kind));
}

FtpProfile ConnectionDialog::profile() const
{
    FtpProfile p = m_original;
    p.kind = DeviceKind(m_kind->currentData().toInt());
    p.name = m_name->text().trimmed();
    if (p.name.isEmpty())
        p.name = Devices::kindName(p.kind);
    p.host = m_host->text().trimmed();
    p.port = m_port->value();
    p.user = m_user->text().trimmed();
    p.password = m_password->text();
    return p;
}

void ConnectionDialog::test()
{
    const FtpProfile p = profile();
    if (p.host.isEmpty()) {
        Dialogs::error(this, QStringLiteral("Укажите IP-адрес"), QStringLiteral("IP-адрес показан на экране устройства."));
        return;
    }
    int count = 0;
    const JobResult r = JobDialog::run(this, QStringLiteral("Проверка подключения"), [&](JobState& st) {
        st.setCurrent(QStringLiteral("%1:%2").arg(p.host).arg(p.port));
        FtpBackend b(p);
        count = int(b.list(QStringLiteral("/")).size());
    });
    if (r.ok())
        Dialogs::info(this, QStringLiteral("Подключено!"), QStringLiteral("Устройство отвечает, в корне %1 объектов.").arg(count));
    else if (!r.cancelled)
        Dialogs::error(this, QStringLiteral("Не удалось подключиться"), r.errors.join(QLatin1Char('\n')));
}

void ConnectionDialog::accept()
{
    if (m_host->text().trimmed().isEmpty()) {
        Dialogs::error(this, QStringLiteral("Укажите IP-адрес"), QStringLiteral("IP-адрес показан на экране устройства."));
        return;
    }
    QDialog::accept();
}
