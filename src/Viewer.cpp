#include "Viewer.h"

#include "Dialogs.h"
#include "FileTypes.h"
#include "JobDialog.h"
#include "Tools.h"

#include <QAudioOutput>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {

constexpr qint64 kMaxTextBytes = 8LL << 20;

bool isTextLike(FileCat cat, const QString& ext)
{
    static const QStringList binaryDocs = {
        QStringLiteral("doc"), QStringLiteral("docx"), QStringLiteral("xls"), QStringLiteral("xlsx"),
        QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odt"), QStringLiteral("ods"),
        QStringLiteral("pages"), QStringLiteral("numbers"), QStringLiteral("key"), QStringLiteral("epub"),
        QStringLiteral("mobi"), QStringLiteral("rtf"),
    };
    return (cat == FileCat::Text || cat == FileCat::Code) && !binaryDocs.contains(ext)
        && ext != QLatin1String("sfo");
}

// Copies a device file into the cache (keeping its name) so Mac apps can open it.
QString fetchToCache(QWidget* parent, const BackendPtr& b, const QString& path, const Entry& e)
{
    if (b->isLocal())
        return path;
    const QByteArray key = QCryptographicHash::hash((b->id() + path).toUtf8(), QCryptographicHash::Md5).toHex();
    const QString dir = Tools::cacheDir(QStringLiteral("open/") + QString::fromLatin1(key));
    const QString local = dir + QLatin1Char('/') + e.name;
    if (QFileInfo(local).size() == e.size && e.size >= 0)
        return local;

    const JobResult r = JobDialog::run(parent, QStringLiteral("Загрузка с устройства"), [&](JobState& st) {
        st.setCurrent(e.name);
        b->download(path, local, [&st, &e](qint64 done, qint64) {
            st.setProgress(done, e.size);
            return !st.cancel.load();
        });
    });
    if (!r.ok()) {
        if (!r.cancelled)
            Dialogs::error(parent, QStringLiteral("Не удалось открыть файл"), r.errors.join(QLatin1Char('\n')));
        QFile::remove(local);
        return {};
    }
    return local;
}

QWidget* textViewer(const QString& title, const QString& localPath)
{
    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly))
        return nullptr;
    const QByteArray data = f.read(kMaxTextBytes);
    auto* edit = new QPlainTextEdit;
    edit->setAttribute(Qt::WA_DeleteOnClose);
    edit->setWindowTitle(title);
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlainText(QString::fromUtf8(data));
    edit->resize(900, 680);
    return edit;
}

void openVideo(QWidget* parent, const BackendPtr& b, const QString& path, const Entry& e)
{
    // What the macOS media engine plays by itself; other formats go to IINA / VLC.
    static const QStringList native = {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("3gp"),
    };
    if (!b->isLocal() && e.size > (512LL << 20)
        && !Dialogs::confirm(parent, QStringLiteral("Скачать видео для просмотра?"),
                             QStringLiteral("«%1» весит %2. Для просмотра его нужно сначала скопировать на Mac.")
                                 .arg(e.name, Tools::humanSize(e.size)),
                             QStringLiteral("Скачать и смотреть"))) {
        return;
    }
    const QString local = fetchToCache(parent, b, path, e);
    if (local.isEmpty())
        return;
#ifdef Q_OS_WIN
    const bool builtIn = true;   // the Windows build plays every format through FFmpeg
#else
    const bool builtIn = native.contains(extensionOf(e.name));
#endif
    if (builtIn) {
        (new VideoViewer(e.name, QUrl::fromLocalFile(local)))->show();
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(local)))
        Dialogs::error(parent, QStringLiteral("Нет видеоплеера для этого формата"),
                       QStringLiteral("Установите IINA или VLC — например, командой: brew install --cask iina"));
}

QString formatTime(qint64 ms)
{
    const qint64 s = ms / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

namespace Viewer {

void open(QWidget* parent, const BackendPtr& b, const QString& dir, const Entry& e, const QList<Entry>& siblings)
{
    const QString path = joinPath(dir, e.name);
    const TypeInfo t = classify(e);
    const QString ext = extensionOf(e.name);

    if (t.cat == FileCat::Image && ext != QLatin1String("svg")) {
        QList<Entry> images;
        int index = 0;
        for (const Entry& s : siblings) {
            if (!s.isDir && classify(s).cat == FileCat::Image) {
                if (s.name == e.name)
                    index = int(images.size());
                images.append(s);
            }
        }
        if (images.isEmpty())
            images.append(e);
        (new ImageViewer(b, dir, images, index))->show();
        return;
    }
    if (t.cat == FileCat::Video) {
        openVideo(parent, b, path, e);
        return;
    }
    if (isTextLike(t.cat, ext) && e.size >= 0 && e.size <= kMaxTextBytes) {
        const QString local = fetchToCache(parent, b, path, e);
        if (local.isEmpty())
            return;
        if (QWidget* w = textViewer(e.name, local))
            w->show();
        return;
    }

    if (!b->isLocal() && !Dialogs::confirm(parent, QStringLiteral("Открыть «%1»?").arg(e.name),
                                           QStringLiteral("Файл будет скопирован на Mac (%1) и открыт в программе по умолчанию.")
                                               .arg(Tools::humanSize(e.size)),
                                           QStringLiteral("Скачать и открыть"))) {
        return;
    }
    const QString local = fetchToCache(parent, b, path, e);
    if (!local.isEmpty() && !QDesktopServices::openUrl(QUrl::fromLocalFile(local)))
        Dialogs::error(parent, QStringLiteral("Нет программы для этого файла"),
                       QStringLiteral("macOS не знает, чем открыть «%1».").arg(e.name));
}

} // namespace Viewer

// ---------------------------------------------------------------------------

ImageViewer::ImageViewer(const BackendPtr& b, const QString& dir, const QList<Entry>& images, int index)
    : m_backend(b), m_dir(dir), m_images(images)
{
    setAttribute(Qt::WA_DeleteOnClose);
    m_label = new QLabel;
    m_label->setAlignment(Qt::AlignCenter);
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_label);
    m_scroll->setWidgetResizable(false);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setFrameShape(QFrame::NoFrame);
    QPalette pal = m_scroll->palette();
    pal.setColor(QPalette::Window, QColor(24, 24, 28));
    m_scroll->setPalette(pal);
    m_scroll->setBackgroundRole(QPalette::Window);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scroll);
    resize(1000, 720);
    showIndex(index);
}

void ImageViewer::showIndex(int index)
{
    if (index < 0 || index >= m_images.size())
        return;
    m_index = index;
    const Entry& e = m_images[index];
    const QString path = joinPath(m_dir, e.name);
    QString local = path;
    if (!m_backend->isLocal())
        local = fetchToCache(this, m_backend, path, e);
    QImageReader reader(local);
    reader.setAutoTransform(true);
    m_image = reader.read();
    m_zoom = 0;
    setWindowTitle(QStringLiteral("%1 — %2 из %3").arg(e.name).arg(index + 1).arg(m_images.size()));
    if (m_image.isNull())
        m_label->setText(QStringLiteral("Не удалось показать изображение"));
    render();
}

void ImageViewer::render()
{
    if (m_image.isNull()) {
        m_label->adjustSize();
        return;
    }
    const qreal dpr = devicePixelRatioF();
    QSize target;
    if (m_zoom <= 0) {
        const QSize avail = m_scroll->viewport()->size() * dpr;
        target = m_image.size();
        if (target.width() > avail.width() || target.height() > avail.height())
            target = target.scaled(avail, Qt::KeepAspectRatio);
    } else {
        target = m_image.size() * m_zoom;
    }
    QPixmap pm = QPixmap::fromImage(m_image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pm.setDevicePixelRatio(dpr);
    m_label->setPixmap(pm);
    m_label->resize(pm.deviceIndependentSize().toSize());
}

void ImageViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_Space:
        showIndex(m_index + 1);
        return;
    case Qt::Key_Left:
    case Qt::Key_Up:
        showIndex(m_index - 1);
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_zoom = (m_zoom <= 0 ? double(m_label->width()) * devicePixelRatioF() / qMax(1, m_image.width()) : m_zoom) * 1.25;
        render();
        return;
    case Qt::Key_Minus:
        m_zoom = (m_zoom <= 0 ? double(m_label->width()) * devicePixelRatioF() / qMax(1, m_image.width()) : m_zoom) / 1.25;
        render();
        return;
    case Qt::Key_0:
        m_zoom = 0;
        render();
        return;
    case Qt::Key_Escape:
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ImageViewer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_zoom <= 0)
        render();
}

// ---------------------------------------------------------------------------

VideoViewer::VideoViewer(const QString& title, const QUrl& source)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);

    auto* video = new QVideoWidget(this);
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(video);

    m_play = new QToolButton(this);
    m_play->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_play->setAutoRaise(true);
    connect(m_play, &QToolButton::clicked, this, &VideoViewer::togglePlay);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_time = new QLabel(QStringLiteral("00:00 / 00:00"), this);
    m_time->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* volume = new QSlider(Qt::Horizontal, this);
    volume->setRange(0, 100);
    volume->setValue(80);
    volume->setFixedWidth(90);
    m_audio->setVolume(0.8f);
    connect(volume, &QSlider::valueChanged, this, [this](int v) { m_audio->setVolume(float(v) / 100.0f); });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        m_slider->setRange(0, int(d));
        updateTime();
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 p) {
        if (!m_seeking)
            m_slider->setValue(int(p));
        updateTime();
    });
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_seeking = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_seeking = false;
        m_player->setPosition(m_slider->value());
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        m_play->setIcon(style()->standardIcon(s == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause
                                                                               : QStyle::SP_MediaPlay));
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this, source](QMediaPlayer::Error, const QString& msg) {
        if (Dialogs::confirm(this, QStringLiteral("Встроенный плеер не справился"),
                             msg + QStringLiteral("\n\nОткрыть видео в другой программе (IINA, VLC)?"),
                             QStringLiteral("Открыть"))) {
            QDesktopServices::openUrl(source);
        }
        close();
    });

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(10, 6, 10, 8);
    controls->addWidget(m_play);
    controls->addWidget(m_slider, 1);
    controls->addWidget(m_time);
    controls->addSpacing(8);
    controls->addWidget(volume);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(video, 1);
    layout->addLayout(controls);
    resize(1000, 640);

    m_player->setSource(source);
    m_player->play();
}

VideoViewer::~VideoViewer()
{
    m_player->stop();
}

void VideoViewer::togglePlay()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void VideoViewer::updateTime()
{
    m_time->setText(formatTime(m_player->position()) + QStringLiteral(" / ") + formatTime(m_player->duration()));
}

void VideoViewer::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        togglePlay();
        return;
    case Qt::Key_Left:
        m_player->setPosition(qMax<qint64>(0, m_player->position() - 5000));
        return;
    case Qt::Key_Right:
        m_player->setPosition(m_player->position() + 5000);
        return;
    case Qt::Key_F:
        isFullScreen() ? showNormal() : showFullScreen();
        return;
    case Qt::Key_Escape:
        if (isFullScreen())
            showNormal();
        else
            close();
        return;
    }
    QWidget::keyPressEvent(event);
}
