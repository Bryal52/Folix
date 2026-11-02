#include "Preview.h"

#include "AdbBackend.h"
#include "FileTypes.h"
#include "Tools.h"

#include <QApplication>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QtEndian>

namespace {

const QSize kMaxPreview(360, 270);
constexpr qint64 kMaxImageBytes = 64LL << 20;
constexpr qint64 kMaxAdbVideoPull = 300LL << 20;

QImage loadImage(QIODevice* dev, QStringList* lines)
{
    QImageReader reader(dev);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid()) {
        if (lines)
            lines->append(QStringLiteral("%1 × %2").arg(full.width()).arg(full.height()));
        if (full.width() > kMaxPreview.width() * 2 || full.height() > kMaxPreview.height() * 2)
            reader.setScaledSize(full.scaled(kMaxPreview * 2, Qt::KeepAspectRatio));
    }
    QImage img = reader.read();
    if (!img.isNull() && lines && !full.isValid())
        lines->append(QStringLiteral("%1 × %2").arg(img.width()).arg(img.height()));
    return img;
}

QString formatDuration(double secs)
{
    const int s = int(secs + 0.5);
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QStringList frameArgs(double at, const QString& input)
{
    QStringList args{QStringLiteral("-v"), QStringLiteral("error")};
    if (at > 0)
        args << QStringLiteral("-ss") << QString::number(at, 'f', 2);
    args << QStringLiteral("-i") << input << QStringLiteral("-an") << QStringLiteral("-sn")
         << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-vf") << QStringLiteral("scale='min(720,iw)':-2")
         << QStringLiteral("-f") << QStringLiteral("image2pipe") << QStringLiteral("-vcodec") << QStringLiteral("mjpeg")
         << QStringLiteral("-q:v") << QStringLiteral("4") << QStringLiteral("pipe:1");
    return args;
}

void probeVideo(const QString& input, double* duration, QStringList* lines)
{
    const auto r = Tools::run(Tools::ffprobe(),
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
         QStringLiteral("-show_entries"), QStringLiteral("stream=width,height:format=duration"),
         QStringLiteral("-of"), QStringLiteral("default=nw=1"), input},
        20000);
    int w = 0, h = 0;
    for (const QByteArray& line : r.out.split('\n')) {
        const QByteArray l = line.trimmed();
        if (l.startsWith("width="))
            w = l.mid(6).toInt();
        else if (l.startsWith("height="))
            h = l.mid(7).toInt();
        else if (l.startsWith("duration="))
            *duration = l.mid(9).toDouble();
    }
    if (w > 0 && h > 0)
        lines->append(QStringLiteral("%1 × %2").arg(w).arg(h));
    if (*duration > 0)
        lines->append(formatDuration(*duration));
}

QImage videoFrame(const QString& input, double duration)
{
    const double at = duration > 0 ? qMin(duration * 0.2, 90.0) : 1.0;
    auto r = Tools::run(Tools::ffmpeg(), frameArgs(at, input), 25000);
    QImage img = QImage::fromData(r.out);
    if (img.isNull()) {
        r = Tools::run(Tools::ffmpeg(), frameArgs(0, input), 25000);
        img = QImage::fromData(r.out);
    }
    return img;
}

// Streams the file from the phone straight into ffmpeg (no seeking possible).
QImage adbPipedFrame(const AdbBackend& adb, const QString& path)
{
    QProcess src, dst;
    src.setStandardOutputProcess(&dst);
    dst.start(Tools::ffmpeg(), frameArgs(0, QStringLiteral("pipe:0")));
    src.start(Tools::adb(), {QStringLiteral("-s"), adb.serial(), QStringLiteral("exec-out"),
                             QStringLiteral("cat ") + Tools::shellQuote(path)});
    dst.waitForFinished(25000);
    src.kill();
    src.waitForFinished(2000);
    return QImage::fromData(dst.readAllStandardOutput());
}

PreviewData buildVideo(const BackendPtr& b, const QString& path, const Entry& e)
{
    PreviewData d;
    if (Tools::ffmpeg().isEmpty()) {
        d.lines << QStringLiteral("Для превью видео нужен ffmpeg");
        return d;
    }
    double duration = 0;
    if (b->isLocal() || !b->mediaUrl(path).isEmpty()) {
        const QString input = b->isLocal() ? path : b->mediaUrl(path);
        probeVideo(input, &duration, &d.lines);
        d.image = videoFrame(input, duration);
        return d;
    }
    auto* adb = dynamic_cast<AdbBackend*>(b.get());
    if (!adb)
        return d;
    if (e.size > 0 && e.size <= kMaxAdbVideoPull) {
        const QByteArray key = QCryptographicHash::hash((b->id() + path + QString::number(e.size)).toUtf8(),
                                                        QCryptographicHash::Md5).toHex();
        const QString local = Tools::cacheDir(QStringLiteral("preview")) + QLatin1Char('/')
            + QString::fromLatin1(key) + QLatin1Char('.') + extensionOf(e.name);
        if (QFileInfo(local).size() != e.size)
            b->download(path, local, nullptr);
        probeVideo(local, &duration, &d.lines);
        d.image = videoFrame(local, duration);
    } else {
        d.image = adbPipedFrame(*adb, path);
    }
    return d;
}

PreviewData buildGameFolder(const BackendPtr& b, const QString& folder)
{
    PreviewData d;
    static const QStringList covers = {
        QStringLiteral("ICON0.PNG"), QStringLiteral("PS3_GAME/ICON0.PNG"), QStringLiteral("sce_sys/icon0.png"),
        QStringLiteral("icon0.png"), QStringLiteral("PIC1.PNG"),
    };
    static const QStringList sfos = {
        QStringLiteral("PARAM.SFO"), QStringLiteral("PS3_GAME/PARAM.SFO"), QStringLiteral("sce_sys/param.sfo"),
    };
    for (const QString& c : covers) {
        try {
            QByteArray data = b->read(joinPath(folder, c), 8LL << 20);
            QBuffer buf(&data);
            buf.open(QIODevice::ReadOnly);
            d.image = loadImage(&buf, nullptr);
            if (!d.image.isNull())
                break;
        } catch (const FsError&) {
        }
    }
    for (const QString& s : sfos) {
        try {
            d.title = Preview::parseSfoTitle(b->read(joinPath(folder, s), 64 * 1024));
            if (!d.title.isEmpty())
                break;
        } catch (const FsError&) {
        }
    }
    return d;
}

} // namespace

namespace Preview {

bool wants(const Entry& e)
{
    if (e.name == QLatin1String(".."))
        return false;
    const FileCat cat = classify(e).cat;
    return cat == FileCat::Image || cat == FileCat::Video || cat == FileCat::GameFolder;
}

PreviewData build(const BackendPtr& b, const QString& dir, const Entry& e)
{
    const QString path = joinPath(dir, e.name);
    const FileCat cat = classify(e).cat;
    try {
        if (cat == FileCat::GameFolder)
            return buildGameFolder(b, path);
        if (cat == FileCat::Video)
            return buildVideo(b, path, e);
        if (cat == FileCat::Image) {
            PreviewData d;
            if (b->isLocal()) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly))
                    d.image = loadImage(&f, &d.lines);
            } else if (e.size <= kMaxImageBytes) {
                QByteArray data = b->read(path, kMaxImageBytes);
                QBuffer buf(&data);
                buf.open(QIODevice::ReadOnly);
                d.image = loadImage(&buf, &d.lines);
            }
            return d;
        }
    } catch (const FsError& err) {
        PreviewData d;
        d.lines << err.message();
        return d;
    }
    return {};
}

QString parseSfoTitle(const QByteArray& d)
{
    if (d.size() < 20 || !d.startsWith(QByteArray("\0PSF", 4)))
        return {};
    auto u32 = [&](qsizetype o) { return qFromLittleEndian<quint32>(d.constData() + o); };
    auto u16 = [&](qsizetype o) { return qFromLittleEndian<quint16>(d.constData() + o); };
    const quint32 keyTable = u32(8), dataTable = u32(12), count = u32(16);
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype base = 20 + qsizetype(i) * 16;
        if (base + 16 > d.size())
            break;
        const qsizetype keyPos = qsizetype(keyTable) + u16(base);
        const quint32 len = u32(base + 4);
        const qsizetype dataPos = qsizetype(dataTable) + u32(base + 12);
        if (keyPos >= d.size() || dataPos + len > d.size())
            continue;
        if (QByteArray(d.constData() + keyPos) == "TITLE")
            return QString::fromUtf8(d.constData() + dataPos, qsizetype(len)).remove(QChar(0)).simplified();
    }
    return {};
}

} // namespace Preview

// ---------------------------------------------------------------------------

PreviewPopup* PreviewPopup::instance()
{
    static PreviewPopup* popup = new PreviewPopup;
    return popup;
}

PreviewPopup::PreviewPopup()
    : QFrame(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    m_name = new QLabel(this);
    m_name->setTextFormat(Qt::PlainText);
    m_name->setWordWrap(true);
    m_name->setMaximumWidth(kMaxPreview.width());
    QFont f = m_name->font();
    f.setBold(true);
    m_name->setFont(f);
    m_info = new QLabel(this);
    m_info->setTextFormat(Qt::PlainText);
    m_info->setWordWrap(true);
    m_info->setMaximumWidth(kMaxPreview.width());
    m_info->setForegroundRole(QPalette::PlaceholderText);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(6);
    layout->addWidget(m_image, 0, Qt::AlignHCenter);
    layout->addWidget(m_name);
    layout->addWidget(m_info);
}

void PreviewPopup::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor bg = palette().color(QPalette::Window);
    bg.setAlpha(246);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
    p.fillPath(path, bg);
    p.setPen(QPen(palette().color(QPalette::Mid), 1));
    p.drawPath(path);
}

void PreviewPopup::showFor(const BackendPtr& b, const QString& dir, const Entry& e, const QPoint& globalPos)
{
    const quint64 gen = ++m_gen;
    m_anchor = globalPos;

    const QString key = b->id() + QLatin1Char('|') + joinPath(dir, e.name) + QLatin1Char('|')
        + QString::number(e.size) + QLatin1Char('|') + QString::number(e.mtime.toSecsSinceEpoch());
    if (const PreviewData* cached = m_cache.object(key)) {
        fill(*cached, e);
        return;
    }

    PreviewData loading;
    loading.lines << QStringLiteral("Загружаю превью…");
    fill(loading, e);
    m_image->setPixmap(fileIcon(classify(e).cat).pixmap(96, 96));

    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;
        p->setMaxThreadCount(2);
        return p;
    }();
    auto* watcher = new QFutureWatcher<PreviewData>(this);
    connect(watcher, &QFutureWatcher<PreviewData>::finished, this, [this, watcher, gen, key, e] {
        const PreviewData d = watcher->result();
        watcher->deleteLater();
        m_cache.insert(key, new PreviewData(d));
        if (gen == m_gen && isVisible())
            fill(d, e);
    });
    watcher->setFuture(QtConcurrent::run(pool, [b, dir, e] { return Preview::build(b, dir, e); }));
}

void PreviewPopup::fill(const PreviewData& d, const Entry& e)
{
    if (!d.image.isNull()) {
        const qreal dpr = devicePixelRatioF();
        QPixmap pm = QPixmap::fromImage(d.image.scaled(kMaxPreview * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        pm.setDevicePixelRatio(dpr);
        m_image->setPixmap(pm);
    } else {
        m_image->setPixmap(fileIcon(classify(e).cat).pixmap(96, 96));
    }
    m_name->setText(d.title.isEmpty() ? e.name : d.title);

    QStringList info;
    const TypeInfo t = classify(e);
    info << t.description;
    if (!e.isDir && e.size >= 0)
        info << Tools::humanSize(e.size);
    info += d.lines;
    m_info->setText(info.join(QStringLiteral(" · ")));

    adjustSize();
    place();
    show();
    raise();
}

void PreviewPopup::place()
{
    QPoint pos = m_anchor + QPoint(18, 18);
    const QScreen* screen = QGuiApplication::screenAt(m_anchor);
    if (screen) {
        const QRect avail = screen->availableGeometry();
        if (pos.x() + width() > avail.right())
            pos.setX(m_anchor.x() - width() - 12);
        if (pos.y() + height() > avail.bottom())
            pos.setY(qMax(avail.top(), avail.bottom() - height()));
    }
    move(pos);
}

void PreviewPopup::hidePopup()
{
    ++m_gen;
    hide();
}
