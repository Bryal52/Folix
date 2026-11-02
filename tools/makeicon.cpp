// Turns the square logo (rounded tile on a black background) into a macOS icon master:
// 1024×1024, transparent corners, tile on Apple's 824 px grid with a soft shadow.
// Usage: makeicon <logo.png> <out.png> [out.ico]   (the .ico is for the Windows build)

#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <cstdio>

namespace {

int brightness(const QImage& img, int x, int y)
{
    const QRgb c = img.pixel(x, y);
    return qMax(qRed(c), qMax(qGreen(c), qBlue(c)));
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: makeicon <logo.png> <out.png>\n");
        return 2;
    }
    const QImage src = QImage(QString::fromLocal8Bit(argv[1])).convertToFormat(QImage::Format_ARGB32);
    if (src.isNull()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    // Find the tile: first pixels brighter than the black background along the middle lines.
    const int w = src.width(), h = src.height();
    const int threshold = 10;
    int left = 0, right = w - 1, top = 0, bottom = h - 1;
    while (left < w / 2 && brightness(src, left, h / 2) < threshold)
        ++left;
    while (right > w / 2 && brightness(src, right, h / 2) < threshold)
        --right;
    while (top < h / 2 && brightness(src, w / 2, top) < threshold)
        ++top;
    while (bottom > h / 2 && brightness(src, w / 2, bottom) < threshold)
        --bottom;
    const QRectF tile(left, top, right - left + 1, bottom - top + 1);
    std::printf("tile: x=%d y=%d w=%d h=%d\n", left, top, right - left + 1, bottom - top + 1);

    const QRectF target(100, 100, 824, 824);
    const qreal radius = 824 * 0.225;

    QImage out(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

    // soft shadow under the tile
    p.setPen(Qt::NoPen);
    for (int i = 14; i >= 1; --i) {
        QPainterPath s;
        s.addRoundedRect(target.adjusted(-i * 0.8, -i * 0.3 + 10, i * 0.8, i * 1.1 + 10), radius + i, radius + i);
        p.setBrush(QColor(0, 0, 0, 7));
        p.drawPath(s);
    }

    QPainterPath clip;
    clip.addRoundedRect(target, radius, radius);
    p.setClipPath(clip);
    // crop slightly inside the tile edge so no black fringe survives
    p.drawImage(target, src, tile.adjusted(2, 2, -2, -2));
    p.end();

    if (!out.save(QString::fromLocal8Bit(argv[2]))) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    if (argc > 3) {
        const QImage ico = out.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!ico.save(QString::fromLocal8Bit(argv[3]), "ICO")) {
            std::fprintf(stderr, "cannot write %s\n", argv[3]);
            return 1;
        }
    }
    return 0;
}
