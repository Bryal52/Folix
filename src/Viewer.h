#pragma once

#include "Backend.h"

#include <QImage>
#include <QUrl>
#include <QWidget>

class QLabel;
class QScrollArea;
class QMediaPlayer;
class QAudioOutput;
class QSlider;
class QToolButton;

namespace Viewer {
// Opens a file: built-in viewer for photos, videos and text, the default Mac app for everything else.
void open(QWidget* parent, const BackendPtr& b, const QString& dir, const Entry& e, const QList<Entry>& siblings);
}

class ImageViewer : public QWidget {
    Q_OBJECT
public:
    ImageViewer(const BackendPtr& b, const QString& dir, const QList<Entry>& images, int index);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void showIndex(int index);
    void render();

    BackendPtr m_backend;
    QString m_dir;
    QList<Entry> m_images;
    int m_index = 0;
    QImage m_image;
    double m_zoom = 0;   // 0 = fit to window
    QScrollArea* m_scroll;
    QLabel* m_label;
};

class VideoViewer : public QWidget {
    Q_OBJECT
public:
    VideoViewer(const QString& title, const QUrl& source);
    ~VideoViewer() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void togglePlay();
    void updateTime();

    QMediaPlayer* m_player;
    QAudioOutput* m_audio;
    QSlider* m_slider;
    QLabel* m_time;
    QToolButton* m_play;
    bool m_seeking = false;
};
