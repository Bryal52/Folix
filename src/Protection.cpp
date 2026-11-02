#include "Protection.h"

namespace {

struct Rule {
    DeviceKind kind;
    const char* prefix;
    Guard level;
    const char* reason;
};

const Rule kRules[] = {
    {DeviceKind::PS3, "/dev_flash", Guard::Block, "Системная прошивка PS3 (dev_flash). Изменения могут превратить консоль в кирпич."},
    {DeviceKind::PS3, "/dev_blind", Guard::Block, "Системная прошивка PS3 (dev_blind). Изменения могут превратить консоль в кирпич."},
    {DeviceKind::PS3, "/dev_hdd0/vsh", Guard::Warn, "Системные данные PS3."},
    {DeviceKind::PS3, "/dev_hdd0/home", Guard::Warn, "Профили пользователей и сохранения PS3."},
    {DeviceKind::PS3, "/dev_hdd0/boot_plugins.txt", Guard::Warn, "Список плагинов, загружаемых при старте."},

    {DeviceKind::Vita, "/vs0:", Guard::Block, "Системный раздел PS Vita (vs0)."},
    {DeviceKind::Vita, "/os0:", Guard::Block, "Системный раздел PS Vita (os0)."},
    {DeviceKind::Vita, "/sa0:", Guard::Block, "Системный раздел PS Vita (sa0)."},
    {DeviceKind::Vita, "/pd0:", Guard::Block, "Системный раздел PS Vita (pd0)."},
    {DeviceKind::Vita, "/tm0:", Guard::Block, "Системный раздел PS Vita (tm0)."},
    {DeviceKind::Vita, "/ux0:/id.dat", Guard::Warn, "Файл привязки карты памяти к консоли."},
    {DeviceKind::Vita, "/ux0:/tai", Guard::Warn, "Настройки плагинов taiHEN. Без них взлом может не загрузиться."},
    {DeviceKind::Vita, "/ur0:/tai", Guard::Warn, "Настройки плагинов taiHEN. Без них взлом может не загрузиться."},
    {DeviceKind::Vita, "/imc0:/tai", Guard::Warn, "Настройки плагинов taiHEN."},
    {DeviceKind::Vita, "/ux0:/user", Guard::Warn, "Сохранения игр PS Vita."},

    {DeviceKind::Switch, "/atmosphere", Guard::Warn, "Кастомная прошивка Atmosphère."},
    {DeviceKind::Switch, "/bootloader", Guard::Warn, "Загрузчик Hekate."},
    {DeviceKind::Switch, "/Nintendo", Guard::Warn, "Данные консоли: установленные игры и сохранения."},
    {DeviceKind::Switch, "/emuMMC", Guard::Warn, "Эмулированная системная память (emuMMC)."},
    {DeviceKind::Switch, "/payload.bin", Guard::Warn, "Загрузочный файл."},
    {DeviceKind::Switch, "/boot.dat", Guard::Warn, "Загрузочный файл."},
    {DeviceKind::Switch, "/boot.ini", Guard::Warn, "Загрузочный файл."},

    {DeviceKind::Android, "/storage/emulated/0/Android", Guard::Warn, "Данные приложений Android."},
};

QString norm(const QString& path)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith(QLatin1Char('/')))
        p.chop(1);
    return p.toLower();
}

bool within(const QString& path, const QString& prefix)
{
    return path == prefix || path.startsWith(prefix + QLatin1Char('/'));
}

} // namespace

GuardResult guardForModify(DeviceKind kind, const QString& path)
{
    const QString p = norm(path);
    GuardResult result;
    for (const Rule& r : kRules) {
        if (r.kind != kind)
            continue;
        const QString prefix = QString::fromLatin1(r.prefix).toLower();
        // the path is protected, or it contains something protected
        const bool hit = within(p, prefix) || p == QLatin1String("/") || within(prefix, p);
        if (hit && r.level > result.level)
            result = {r.level, QString::fromUtf8(r.reason)};
    }
    return result;
}

GuardResult guardForWriteInto(DeviceKind kind, const QString& dir)
{
    const QString p = norm(dir);
    for (const Rule& r : kRules) {
        if (r.kind == kind && r.level == Guard::Block && within(p, QString::fromLatin1(r.prefix).toLower()))
            return {Guard::Block, QString::fromUtf8(r.reason)};
    }
    return {};
}
