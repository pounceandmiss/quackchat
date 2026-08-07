#include "QImageAvatarEncoder.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

namespace {
// FileDialog hands back file:// on desktop and content:// on Android, where
// toLocalFile() is empty and Qt's own content file engine reads the URL as it
// stands. A bare path (a test fixture, say) is already what QImageReader wants.
QString readablePath(const QUrl &url) {
    if (url.isLocalFile())
        return url.toLocalFile();
    return url.toString();
}
} // namespace

AvatarImage QImageAvatarEncoder::encode(const QUrl &source,
                                        QString *error) const {
    AvatarImage out;

    QImageReader reader(readablePath(source));
    reader.setAutoTransform(true); // or a phone photo publishes sideways
    const QImage decoded = reader.read();
    if (decoded.isNull()) {
        if (error)
            *error = QStringLiteral("Unsupported image format");
        return out;
    }

    // Centre-crop to a square first, so scaling never stretches the face.
    const int side = qMin(decoded.width(), decoded.height());
    QImage img = decoded.copy((decoded.width() - side) / 2,
                              (decoded.height() - side) / 2, side, side);
    if (side != kEdge)
        img = img.scaled(kEdge, kEdge, Qt::IgnoreAspectRatio,
                         Qt::SmoothTransformation);

    QBuffer buf(&out.png);
    buf.open(QIODevice::WriteOnly);
    if (!img.save(&buf, "PNG") || out.png.isEmpty()) {
        out.png.clear();
        if (error)
            *error = QStringLiteral("Could not encode the picture");
        return out;
    }
    out.width = img.width();
    out.height = img.height();
    return out;
}
