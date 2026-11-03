#include "FileTypes.h"

#include <QConicalGradient>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>

// ---------------------------------------------------------------------------
// Classification

namespace {

struct ExtInfo {
    FileCat cat;
    const char* description;   // empty = use the category's default text
};

const QHash<QString, ExtInfo>& extTable()
{
    static const QHash<QString, ExtInfo> table = [] {
        QHash<QString, ExtInfo> t;
        auto add = [&](std::initializer_list<const char*> exts, FileCat cat, const char* desc = "") {
            for (const char* e : exts)
                t.insert(QString::fromLatin1(e), {cat, desc});
        };
        add({"apk"}, FileCat::Android, "Приложение Android");
        add({"xapk", "apks", "apkm"}, FileCat::Android, "Пакет Android");
        add({"aab"}, FileCat::Android, "Сборка Android (AAB)");
        add({"exe"}, FileCat::Windows, "Программа Windows");
        add({"msi"}, FileCat::Windows, "Установщик Windows");
        add({"dll"}, FileCat::Windows, "Библиотека Windows");
        add({"bat", "cmd"}, FileCat::Windows, "Скрипт Windows");
        add({"zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "xz", "zst", "lz", "lzma", "cab"}, FileCat::Archive);
        add({"mp4", "m4v", "mkv", "avi", "mov", "wmv", "flv", "webm", "mpg", "mpeg", "ts", "m2ts", "mts", "3gp", "vob", "ogv"},
            FileCat::Video);
        add({"jpg", "jpeg", "png", "gif", "bmp", "webp", "heic", "heif", "tif", "tiff", "ico", "tga", "dng", "avif"},
            FileCat::Image);
        add({"svg"}, FileCat::Image, "Векторное изображение SVG");
        add({"mp3", "flac", "wav", "aac", "m4a", "ogg", "opus", "wma", "aiff", "aif", "alac", "at9", "mid", "midi"},
            FileCat::Audio);
        add({"txt"}, FileCat::Text, "Текстовый файл");
        add({"log"}, FileCat::Text, "Журнал");
        add({"md"}, FileCat::Text, "Текст Markdown");
        add({"ini", "cfg", "conf"}, FileCat::Text, "Файл настроек");
        add({"csv"}, FileCat::Text, "Таблица CSV");
        add({"nfo"}, FileCat::Text, "Описание");
        add({"rtf"}, FileCat::Text, "Документ RTF");
        add({"doc", "docx", "odt", "pages"}, FileCat::Text, "Документ");
        add({"xls", "xlsx", "ods", "numbers"}, FileCat::Text, "Таблица");
        add({"ppt", "pptx", "key"}, FileCat::Text, "Презентация");
        add({"epub", "fb2", "mobi"}, FileCat::Text, "Книга");
        add({"pdf"}, FileCat::Pdf, "Документ PDF");
        add({"json"}, FileCat::Code, "Данные JSON");
        add({"xml", "yml", "yaml", "toml"}, FileCat::Code, "Данные");
        add({"html", "htm", "css", "js", "py", "sh", "c", "cpp", "h", "hpp", "lua", "java", "kt", "swift", "rb", "go", "rs"},
            FileCat::Code);
        add({"sfo"}, FileCat::Code, "Параметры игры (SFO)");
        add({"vpk"}, FileCat::Vita, "Приложение PS Vita (VPK)");
        add({"nsp"}, FileCat::Switch, "Игра Switch (NSP)");
        add({"nsz"}, FileCat::Switch, "Игра Switch (NSZ, сжатая)");
        add({"xci"}, FileCat::Switch, "Картридж Switch (XCI)");
        add({"xcz"}, FileCat::Switch, "Картридж Switch (XCZ, сжатый)");
        add({"nro"}, FileCat::Switch, "Программа Switch (NRO)");
        add({"nca"}, FileCat::Switch, "Контент Switch (NCA)");
        add({"iso"}, FileCat::Disc, "Образ диска ISO");
        add({"cso"}, FileCat::Disc, "Сжатый образ PSP (CSO)");
        add({"bin", "cue", "img", "chd", "mdf", "mds", "nrg"}, FileCat::Disc, "Образ диска");
        add({"dmg"}, FileCat::Disc, "Образ диска macOS");
        add({"pbp"}, FileCat::Disc, "Игра PSP (PBP)");
        add({"pkg"}, FileCat::Package, "Пакет PS3 / PS Vita (PKG)");
        add({"rap"}, FileCat::License, "Лицензия PS3 (RAP)");
        add({"rif"}, FileCat::License, "Лицензия (RIF)");
        add({"edat"}, FileCat::License, "Защищённые данные (EDAT)");
        add({"rifkey", "keys"}, FileCat::License, "Ключи");
        add({"tik"}, FileCat::License, "Тикет Switch");
        add({"sprx"}, FileCat::Plugin, "Плагин PS3 (SPRX)");
        add({"suprx"}, FileCat::Plugin, "Плагин PS Vita (SUPRX)");
        add({"skprx"}, FileCat::Plugin, "Плагин ядра PS Vita (SKPRX)");
        add({"prx"}, FileCat::Plugin, "Плагин PSP (PRX)");
        add({"self", "elf"}, FileCat::Plugin, "Исполняемый файл");
        add({"sav", "srm", "save", "sv", "state", "dsv", "mcr", "psv"}, FileCat::Save, "Сохранение");
        return t;
    }();
    return table;
}

QString defaultDescription(FileCat cat, const QString& extUpper)
{
    switch (cat) {
    case FileCat::Archive: return QStringLiteral("Архив ") + extUpper;
    case FileCat::Video: return QStringLiteral("Видео ") + extUpper;
    case FileCat::Image: return QStringLiteral("Изображение ") + extUpper;
    case FileCat::Audio: return QStringLiteral("Аудио ") + extUpper;
    case FileCat::Code: return QStringLiteral("Код ") + extUpper;
    default:
        return extUpper.isEmpty() ? QStringLiteral("Файл") : QStringLiteral("Файл ") + extUpper;
    }
}

} // namespace

QString extensionOf(const QString& name)
{
    const int dot = int(name.lastIndexOf(QLatin1Char('.')));
    if (dot <= 0 || dot == name.size() - 1)
        return {};
    return name.mid(dot + 1).toLower();
}

bool isGameFolderName(const QString& name)
{
    // PS3 / Vita title IDs (BLES12345, PCSE00000), also "Game Name [BLUS12345]"; Switch title IDs.
    static const QRegularExpression ps(QStringLiteral("(^|[^A-Z])[A-Z]{4}-?\\d{5}([^0-9]|$)"));
    static const QRegularExpression sw(QStringLiteral("^0100[0-9A-Fa-f]{12}$"));
    return ps.match(name).hasMatch() || sw.match(name).hasMatch();
}

TypeInfo classify(const Entry& e)
{
    if (e.isDir) {
        if (e.name == QLatin1String(".."))
            return {FileCat::Up, QString()};
        if (isGameFolderName(e.name))
            return {FileCat::GameFolder, QStringLiteral("Папка игры")};
        if (e.name.endsWith(QLatin1String(".app")))
            return {FileCat::Folder, QStringLiteral("Программа macOS")};
        return {FileCat::Folder, QStringLiteral("Папка")};
    }

    TypeInfo info;
    const QString ext = extensionOf(e.name);
    if (e.name.compare(QLatin1String("EBOOT.BIN"), Qt::CaseInsensitive) == 0) {
        info = {FileCat::Plugin, QStringLiteral("Запускаемый файл игры")};
    } else if (const auto it = extTable().constFind(ext); it != extTable().constEnd()) {
        info.cat = it->cat;
        info.description = *it->description ? QString::fromUtf8(it->description)
                                            : defaultDescription(it->cat, ext.toUpper());
    } else {
        info = {FileCat::Unknown, defaultDescription(FileCat::Unknown, ext.toUpper())};
    }
    if (!e.parts.isEmpty())
        info.description += QStringLiteral(" · частей: %1").arg(e.parts.size());
    return info;
}

// ---------------------------------------------------------------------------
// Painting. Everything is drawn on a 32×32 grid and scaled.

namespace {

QLinearGradient vgrad(qreal y1, qreal y2, const QColor& top, const QColor& bottom)
{
    QLinearGradient g(0, y1, 0, y2);
    g.setColorAt(0, top);
    g.setColorAt(1, bottom);
    return g;
}

void drawPage(QPainter& p)
{
    QPainterPath path;
    path.moveTo(6.5, 2);
    path.lineTo(20, 2);
    path.lineTo(27, 9);
    path.lineTo(27, 28.5);
    path.quadTo(27, 30, 25.5, 30);
    path.lineTo(6.5, 30);
    path.quadTo(5, 30, 5, 28.5);
    path.lineTo(5, 3.5);
    path.quadTo(5, 2, 6.5, 2);
    p.setPen(QPen(QColor(0, 0, 0, 70), 0.6));
    p.setBrush(vgrad(2, 30, QColor(255, 255, 255), QColor(236, 238, 243)));
    p.drawPath(path);

    QPainterPath fold;
    fold.moveTo(20, 2);
    fold.lineTo(20, 7.5);
    fold.quadTo(20, 9, 21.5, 9);
    fold.lineTo(27, 9);
    fold.closeSubpath();
    p.setBrush(QColor(214, 218, 226));
    p.drawPath(fold);
}

void drawTile(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(vgrad(14, 27, c.lighter(118), c));
    p.drawRoundedRect(QRectF(8.5, 14, 15, 13), 2.6, 2.6);
}

void drawBigTile(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(vgrad(2, 30, c.lighter(122), c.darker(108)));
    p.drawRoundedRect(QRectF(2, 2, 28, 28), 7, 7);
}

void drawFolder(QPainter& p, const QColor& front)
{
    const QColor back = front.darker(118);
    p.setPen(Qt::NoPen);
    p.setBrush(back);
    p.drawRoundedRect(QRectF(3, 5, 11, 6), 2, 2);
    p.drawRoundedRect(QRectF(3, 7.5, 26, 20.5), 2.2, 2.2);
    p.setBrush(vgrad(10.5, 28, front.lighter(112), front));
    p.drawRoundedRect(QRectF(3, 10.5, 26, 17.5), 2.2, 2.2);
    p.setPen(QPen(QColor(255, 255, 255, 110), 0.6));
    p.drawLine(QPointF(4.5, 11.2), QPointF(27.5, 11.2));
}

void drawGamepad(QPainter& p, const QPointF& c, qreal s, const QColor& body, const QColor& detail)
{
    p.save();
    p.translate(c);
    p.scale(s, s);
    QPainterPath pad;
    pad.addRoundedRect(QRectF(-6, -3.2, 12, 6.4), 3.2, 3.2);
    pad.addEllipse(QRectF(-6.2, -1.0, 4.4, 5.0));
    pad.addEllipse(QRectF(1.8, -1.0, 4.4, 5.0));
    pad.setFillRule(Qt::WindingFill);
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawPath(pad.simplified());
    p.setBrush(detail);
    p.drawRect(QRectF(-4.6, -0.4, 3.0, 0.9));
    p.drawRect(QRectF(-3.55, -1.45, 0.9, 3.0));
    p.drawEllipse(QPointF(2.6, -0.6), 0.6, 0.6);
    p.drawEllipse(QPointF(3.9, 0.6), 0.6, 0.6);
    p.restore();
}

void drawAndroidHead(QPainter& p, const QColor& c, const QColor& eyes)
{
    p.setPen(QPen(c, 1.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(11.8, 15.0), QPointF(10.3, 12.6));
    p.drawLine(QPointF(20.2, 15.0), QPointF(21.7, 12.6));
    QPainterPath head;
    head.moveTo(9, 21);
    head.arcTo(QRectF(9, 14, 14, 14), 180, -180);
    head.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(head);
    p.drawRoundedRect(QRectF(9, 22, 14, 6), 1.6, 1.6);
    p.setBrush(eyes);
    p.drawEllipse(QPointF(13, 18.2), 0.95, 0.95);
    p.drawEllipse(QPointF(19, 18.2), 0.95, 0.95);
}

void drawHandheld(QPainter& p, const QColor& body, const QColor& screen)
{
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawRoundedRect(QRectF(9.4, 16.8, 13.2, 7.4), 3.7, 3.7);
    p.setBrush(screen);
    p.drawRoundedRect(QRectF(12.6, 17.9, 6.8, 5.2), 0.6, 0.6);
}

void drawJoycons(QPainter& p, qreal x, qreal y, qreal h)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 184, 230));
    p.drawRoundedRect(QRectF(x, y, 3.6, h), 1.8, 1.8);
    p.setBrush(QColor(46, 49, 56));
    p.drawRoundedRect(QRectF(x + 3.9, y, 7.4, h), 0.6, 0.6);
    p.setBrush(QColor(255, 79, 69));
    p.drawRoundedRect(QRectF(x + 11.6, y, 3.6, h), 1.8, 1.8);
}

void drawLines(QPainter& p, const QColor& c, qreal x1, qreal x2, std::initializer_list<qreal> ys)
{
    p.setPen(QPen(c, 1.2, Qt::SolidLine, Qt::RoundCap));
    int i = 0;
    const int n = int(ys.size());
    for (qreal y : ys)
        p.drawLine(QPointF(x1, y), QPointF(++i == n ? x1 + (x2 - x1) * 0.6 : x2, y));
}

void drawDisc(QPainter& p)
{
    QConicalGradient g(QPointF(16, 16), 30);
    g.setColorAt(0.0, QColor(232, 234, 240));
    g.setColorAt(0.2, QColor(201, 216, 245));
    g.setColorAt(0.4, QColor(242, 230, 247));
    g.setColorAt(0.6, QColor(213, 240, 230));
    g.setColorAt(0.8, QColor(245, 240, 214));
    g.setColorAt(1.0, QColor(232, 234, 240));
    QPainterPath disc;
    disc.addEllipse(QPointF(16, 16), 13, 13);
    disc.addEllipse(QPointF(16, 16), 1.9, 1.9);
    disc.setFillRule(Qt::OddEvenFill);
    p.setPen(QPen(QColor(120, 128, 142), 0.6));
    p.setBrush(g);
    p.drawPath(disc);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(150, 158, 172), 1.3));
    p.drawEllipse(QPointF(16, 16), 4.3, 4.3);
}

void drawBox(QPainter& p)
{
    p.setPen(Qt::NoPen);
    p.setBrush(vgrad(11, 29, QColor(222, 171, 99), QColor(196, 138, 62)));
    p.drawRoundedRect(QRectF(5, 11, 22, 18), 1.6, 1.6);
    p.setBrush(QColor(176, 120, 50));
    p.drawRoundedRect(QRectF(4, 7.5, 24, 5.5), 1.4, 1.4);
    p.setBrush(QColor(243, 214, 160));
    p.drawRect(QRectF(14.5, 7.5, 3, 21.5));
    p.setBrush(QColor(255, 255, 255, 230));
    p.drawRoundedRect(QRectF(19, 21, 6, 4.5), 0.6, 0.6);
}

void drawFloppy(QPainter& p)
{
    QPainterPath body;
    body.moveTo(7, 4);
    body.lineTo(23.5, 4);
    body.lineTo(28, 8.5);
    body.lineTo(28, 26);
    body.quadTo(28, 28, 26, 28);
    body.lineTo(6, 28);
    body.quadTo(4, 28, 4, 26);
    body.lineTo(4, 7);
    body.quadTo(4, 4, 7, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(vgrad(4, 28, QColor(66, 118, 196), QColor(40, 84, 160)));
    p.drawPath(body);
    p.setBrush(QColor(206, 211, 220));
    p.drawRect(QRectF(9.5, 4, 12, 8));
    p.setBrush(QColor(40, 84, 160));
    p.drawRect(QRectF(17.2, 5.4, 2.6, 5.2));
    p.setBrush(QColor(250, 250, 252));
    p.drawRoundedRect(QRectF(8, 15.5, 16, 10.5), 1, 1);
    drawLines(p, QColor(170, 176, 188), 10, 22, {18.5, 21.5});
}

void drawKey(QPainter& p, const QColor& c)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(c, 2.2));
    p.drawEllipse(QPointF(12.5, 20), 3.4, 3.4);
    p.setPen(QPen(c, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(16, 20), QPointF(24, 20));
    p.setPen(QPen(c, 1.8, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(21, 20), QPointF(21, 23));
    p.drawLine(QPointF(23.6, 20), QPointF(23.6, 22.4));
}

void drawPuzzle(QPainter& p, const QColor& c)
{
    QPainterPath piece;
    piece.addRoundedRect(QRectF(11, 18, 8.5, 7.2), 0.8, 0.8);
    piece.addEllipse(QPointF(15.25, 17.6), 1.8, 1.8);
    piece.addEllipse(QPointF(19.9, 21.6), 1.8, 1.8);
    piece.setFillRule(Qt::WindingFill);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(piece.simplified());
}

void drawPlay(QPainter& p, const QColor& c)
{
    QPolygonF tri;
    tri << QPointF(13.6, 16.6) << QPointF(13.6, 24.4) << QPointF(20.2, 20.5);
    p.setPen(QPen(c, 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(c);
    p.drawPolygon(tri);
}

void drawMountains(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(19.6, 17.4), 1.4, 1.4);
    QPolygonF m;
    m << QPointF(9.8, 25.4) << QPointF(13.6, 19.4) << QPointF(16.2, 22.6) << QPointF(18.2, 20.6) << QPointF(22.2, 25.4);
    p.drawPolygon(m);
}

void drawNote(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QPointF(13.3, 24), 1.9, 1.45);
    p.drawEllipse(QPointF(19.3, 23), 1.9, 1.45);
    p.drawRect(QRectF(14.4, 16.6, 0.9, 7.4));
    p.drawRect(QRectF(20.4, 15.6, 0.9, 7.4));
    QPolygonF beam;
    beam << QPointF(14.4, 16.6) << QPointF(21.3, 15.4) << QPointF(21.3, 17.3) << QPointF(14.4, 18.5);
    p.drawPolygon(beam);
}

void drawZipper(QPainter& p, const QColor& c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    for (int i = 0; i < 4; ++i) {
        p.drawRect(QRectF(14.6, 14.8 + i * 2.0, 1.4, 1.0));
        p.drawRect(QRectF(16.0, 15.8 + i * 2.0, 1.4, 1.0));
    }
    p.drawRoundedRect(QRectF(14.4, 22.6, 3.2, 3.8), 0.8, 0.8);
}

void drawWindowsLogo(QPainter& p)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 120, 212));
    p.drawRect(QRectF(9.5, 13.5, 6.2, 5.8));
    p.drawRect(QRectF(16.5, 13.5, 6.2, 5.8));
    p.drawRect(QRectF(9.5, 20.1, 6.2, 5.8));
    p.drawRect(QRectF(16.5, 20.1, 6.2, 5.8));
}

void drawCode(QPainter& p)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(84, 104, 140), 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{QPointF(12.6, 16.2), QPointF(9.6, 19.8), QPointF(12.6, 23.4)});
    p.drawPolyline(QPolygonF{QPointF(19.4, 16.2), QPointF(22.4, 19.8), QPointF(19.4, 23.4)});
    p.drawLine(QPointF(17.2, 15.4), QPointF(14.8, 24.2));
}

void drawQuestion(QPainter& p)
{
    QFont f;
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(142, 149, 163));
    p.drawText(QRectF(5, 10, 22, 19), Qt::AlignCenter, QStringLiteral("?"));
}

void drawArrowUp(QPainter& p, const QColor& c)
{
    QPolygonF a;
    a << QPointF(16, 13.2) << QPointF(11, 18.6) << QPointF(14.4, 18.6) << QPointF(14.4, 24.5)
      << QPointF(17.6, 24.5) << QPointF(17.6, 18.6) << QPointF(21, 18.6);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPolygon(a);
}

QIcon renderIcon(const std::function<void(QPainter&)>& paint)
{
    QIcon icon;
    for (int px : {16, 20, 24, 32, 40, 48, 64, 128, 256}) {
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        p.scale(px / 32.0, px / 32.0);
        paint(p);
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

const QColor kFolderBlue(84, 172, 240);
const QColor kWhite(255, 255, 255);

} // namespace

void paintFileIcon(QPainter& p, const QRectF& box, FileCat cat)
{
    p.save();
    p.translate(box.topLeft());
    p.scale(box.width() / 32.0, box.height() / 32.0);
    switch (cat) {
    case FileCat::Folder:
        drawFolder(p, kFolderBlue);
        break;
    case FileCat::GameFolder:
        drawFolder(p, kFolderBlue);
        drawGamepad(p, QPointF(16, 19.6), 1.0, QColor(255, 255, 255, 240), kFolderBlue.darker(115));
        break;
    case FileCat::Up:
        drawFolder(p, QColor(150, 170, 196));
        drawArrowUp(p, kWhite);
        break;
    case FileCat::Android:
        drawPage(p);
        drawAndroidHead(p, QColor(61, 220, 132), kWhite);
        break;
    case FileCat::Windows:
        drawPage(p);
        drawWindowsLogo(p);
        break;
    case FileCat::Archive:
        drawPage(p);
        drawTile(p, QColor(176, 126, 70));
        drawZipper(p, kWhite);
        break;
    case FileCat::Video:
        drawPage(p);
        drawTile(p, QColor(138, 79, 230));
        drawPlay(p, kWhite);
        break;
    case FileCat::Image:
        drawPage(p);
        drawTile(p, QColor(28, 170, 172));
        drawMountains(p, kWhite);
        break;
    case FileCat::Audio:
        drawPage(p);
        drawTile(p, QColor(236, 72, 132));
        drawNote(p, kWhite);
        break;
    case FileCat::Text:
        drawPage(p);
        drawLines(p, QColor(150, 158, 172), 9, 23, {12, 15, 18, 21, 24});
        break;
    case FileCat::Pdf:
        drawPage(p);
        drawTile(p, QColor(226, 54, 54));
        drawLines(p, kWhite, 11.5, 20.5, {17.5, 20.5, 23.5});
        break;
    case FileCat::Code:
        drawPage(p);
        drawCode(p);
        break;
    case FileCat::Vita:
        drawPage(p);
        drawTile(p, QColor(36, 86, 214));
        drawHandheld(p, kWhite, QColor(36, 86, 214));
        break;
    case FileCat::Switch:
        drawPage(p);
        drawJoycons(p, 8.4, 14.6, 10.6);
        break;
    case FileCat::Disc:
        drawDisc(p);
        break;
    case FileCat::Package:
        drawBox(p);
        break;
    case FileCat::License:
        drawPage(p);
        drawKey(p, QColor(222, 158, 20));
        break;
    case FileCat::Plugin:
        drawPage(p);
        drawTile(p, QColor(124, 92, 236));
        drawPuzzle(p, kWhite);
        break;
    case FileCat::Save:
        drawFloppy(p);
        break;
    case FileCat::Unknown:
        drawPage(p);
        drawQuestion(p);
        break;
    }
    p.restore();
}

QIcon fileIcon(FileCat cat)
{
    static QHash<int, QIcon> cache;
    auto it = cache.find(int(cat));
    if (it == cache.end())
        it = cache.insert(int(cat), renderIcon([cat](QPainter& p) { paintFileIcon(p, QRectF(0, 0, 32, 32), cat); }));
    return *it;
}

QIcon deviceIcon(DeviceKind kind)
{
    static QHash<int, QIcon> cache;
    auto it = cache.find(int(kind));
    if (it != cache.end())
        return *it;

    QIcon icon = renderIcon([kind](QPainter& p) {
        switch (kind) {
        case DeviceKind::Mac:
            drawBigTile(p, QColor(120, 132, 150));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWhite, 1.6));
            p.drawRoundedRect(QRectF(9, 9.5, 14, 10), 1.2, 1.2);
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            p.drawRoundedRect(QRectF(6.5, 20.8, 19, 2.4), 1.1, 1.1);
            break;
        case DeviceKind::Android:
            drawBigTile(p, QColor(48, 190, 112));
            p.translate(0, -3);
            drawAndroidHead(p, kWhite, QColor(48, 190, 112));
            break;
        case DeviceKind::Vita:
            drawBigTile(p, QColor(36, 86, 214));
            p.translate(16, 16);
            p.scale(1.5, 1.5);
            p.translate(-16, -20.5);
            drawHandheld(p, kWhite, QColor(36, 86, 214));
            break;
        case DeviceKind::Switch:
            drawBigTile(p, QColor(230, 0, 18));
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            p.drawRoundedRect(QRectF(8.5, 8, 6.2, 16), 3.1, 3.1);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWhite, 1.5));
            p.drawRoundedRect(QRectF(18, 8.7, 5.5, 14.6), 2.7, 2.7);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(230, 0, 18));
            p.drawEllipse(QPointF(11.6, 12), 1.3, 1.3);
            p.setBrush(kWhite);
            p.drawEllipse(QPointF(20.75, 18.5), 1.3, 1.3);
            break;
        case DeviceKind::PS3:
            drawBigTile(p, QColor(40, 40, 46));
            drawGamepad(p, QPointF(16, 16), 1.35, kWhite, QColor(40, 40, 46));
            break;
        case DeviceKind::Other:
            drawBigTile(p, QColor(96, 125, 139));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWhite, 1.4));
            p.drawEllipse(QPointF(16, 16), 8, 8);
            p.drawEllipse(QPointF(16, 16), 3.6, 8);
            p.drawLine(QPointF(8, 16), QPointF(24, 16));
            break;
        case DeviceKind::Archive:
            drawBigTile(p, QColor(210, 74, 62));   // Packix's soft red
            p.translate(0, -4);
            drawZipper(p, kWhite);
            break;
        }
    });
    cache.insert(int(kind), icon);
    return icon;
}

QIcon placeIcon(const QString& place)
{
    static QHash<QString, QIcon> cache;
    auto it = cache.find(place);
    if (it != cache.end())
        return *it;

    QIcon icon = renderIcon([place](QPainter& p) {
        if (place == QLatin1String("home")) {
            drawBigTile(p, QColor(64, 150, 240));
            QPolygonF house;
            house << QPointF(16, 8) << QPointF(25, 16) << QPointF(22.5, 16) << QPointF(22.5, 24)
                  << QPointF(18, 24) << QPointF(18, 19) << QPointF(14, 19) << QPointF(14, 24)
                  << QPointF(9.5, 24) << QPointF(9.5, 16) << QPointF(7, 16);
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            p.drawPolygon(house);
        } else if (place == QLatin1String("desktop")) {
            drawBigTile(p, QColor(94, 106, 210));
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            p.drawRoundedRect(QRectF(7.5, 8.5, 17, 11.5), 1.4, 1.4);
            p.drawRect(QRectF(14.5, 20, 3, 2.6));
            p.drawRoundedRect(QRectF(11, 22.4, 10, 1.8), 0.9, 0.9);
        } else if (place == QLatin1String("downloads")) {
            drawBigTile(p, QColor(52, 160, 240));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(kWhite, 1.5));
            p.drawEllipse(QPointF(16, 16), 8.5, 8.5);
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            QPolygonF a;
            a << QPointF(14.6, 10.5) << QPointF(17.4, 10.5) << QPointF(17.4, 16.5) << QPointF(20.5, 16.5)
              << QPointF(16, 21.5) << QPointF(11.5, 16.5) << QPointF(14.6, 16.5);
            p.drawPolygon(a);
        } else if (place == QLatin1String("documents")) {
            drawBigTile(p, QColor(120, 132, 150));
            p.translate(16, 16);
            p.scale(0.72, 0.72);
            p.translate(-16, -16);
            drawPage(p);
            drawLines(p, QColor(150, 158, 172), 9, 23, {13, 17, 21, 25});
        } else if (place == QLatin1String("apps")) {
            drawBigTile(p, QColor(94, 92, 230));
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            for (int r = 0; r < 2; ++r) {
                for (int c = 0; c < 2; ++c)
                    p.drawRoundedRect(QRectF(8.5 + c * 8, 8.5 + r * 8, 6.5, 6.5), 1.8, 1.8);
            }
        } else if (place == QLatin1String("add")) {
            drawBigTile(p, QColor(52, 199, 89));
            p.setPen(QPen(kWhite, 2.4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(16, 10), QPointF(16, 22));
            p.drawLine(QPointF(10, 16), QPointF(22, 16));
        } else {   // volume
            drawBigTile(p, QColor(120, 132, 150));
            p.setPen(Qt::NoPen);
            p.setBrush(kWhite);
            p.drawRoundedRect(QRectF(7, 11, 18, 10), 2.2, 2.2);
            p.setBrush(QColor(52, 199, 89));
            p.drawEllipse(QPointF(21.5, 16), 1.2, 1.2);
        }
    });
    cache.insert(place, icon);
    return icon;
}
