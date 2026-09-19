#include "Clipboard.h"

#include <QBuffer>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSaveFile>

#include "PickedFile.h"

namespace {

// Formats handed on as they came, since re-encoding a photo through QImage
// would cost quality and size. Anything else - a screenshot's raw pixels, a
// vendor's own format - is written as PNG.
struct Format {
    const char *mime;
    const char *suffix;
};
constexpr Format kVerbatim[] = {{"image/png", "png"},
                                {"image/jpeg", "jpg"},
                                {"image/gif", "gif"},
                                {"image/webp", "webp"}};

} // namespace

QList<QUrl> Clipboard::files() {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasUrls())
        return {};
    QList<QUrl> out;
    for (const QUrl &url : data->urls()) {
        // A copied link offers a url list too, and there is nothing to upload
        // behind it. Only what is on this disk now counts as an attachment.
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile()))
            out << url;
    }
    return out;
}

void Clipboard::setText(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}

QUrl Clipboard::saveImage() {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasImage())
        return {};

    QByteArray bytes;
    QString suffix;
    for (const Format &f : kVerbatim) {
        if (!data->hasFormat(QLatin1String(f.mime)))
            continue;
        bytes = data->data(QLatin1String(f.mime));
        suffix = QLatin1String(f.suffix);
        break;
    }
    if (bytes.isEmpty()) {
        const QImage image = qvariant_cast<QImage>(data->imageData());
        if (image.isNull())
            return {};
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            return {};
        suffix = QStringLiteral("png");
    }

    const QString dir = pickedfile::outgoingDir();
    if (dir.isEmpty())
        return {};
    // The name is all the receiving client has to go on, and the clock in it
    // keeps two pastes in one chat apart.
    const QString path =
        dir + QStringLiteral("/pasted-") +
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz")) +
        QLatin1Char('.') + suffix;
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(bytes);
    if (!out.commit())
        return {};
    return QUrl::fromLocalFile(path);
}
