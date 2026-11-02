#pragma once

#include "Backend.h"

#include <QCache>
#include <QFrame>
#include <QImage>

class QLabel;

struct PreviewData {
    QImage image;
    QString title;       // e.g. game title from PARAM.SFO
    QStringList lines;   // resolution, duration, ...
};

namespace Preview {

bool wants(const Entry& e);
// Blocking: reads the file (or part of it) from the device. Runs on a worker thread.
PreviewData build(const BackendPtr& b, const QString& dir, const Entry& e);
// Full-size frame or image for a local file (used by the viewer).
QString parseSfoTitle(const QByteArray& sfo);

} // namespace Preview

// Floating card shown next to the cursor.
class PreviewPopup : public QFrame {
    Q_OBJECT
public:
    static PreviewPopup* instance();
    void showFor(const BackendPtr& b, const QString& dir, const Entry& e, const QPoint& globalPos);
    void hidePopup();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PreviewPopup();
    void fill(const PreviewData& d, const Entry& e);
    void place();

    QLabel* m_image;
    QLabel* m_name;
    QLabel* m_info;
    QPoint m_anchor;
    quint64 m_gen = 0;
    QCache<QString, PreviewData> m_cache{120};
};
