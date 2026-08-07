// Serves QML `image://avatar/<acc>/<jid>/<hash>` URLs. The <hash> segment is
// only there to cache-bust - a changed avatar yields a URL QQuickPixmapCache
// treats as a fresh image - while the fetch itself keys on acc+jid. Async,
// since the bytes come from tacky over a round-trip.
#ifndef AVATARIMAGEPROVIDER_H
#define AVATARIMAGEPROVIDER_H

#include <QImage>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QString>

#include "AvatarSink.h"

class AvatarController;

// One in-flight request. It owns the QImage decode, which is what keeps QtGui
// out of AvatarController.
class AvatarResponse : public QQuickImageResponse, public AvatarSink {
public:
    // `edge` is the longest side of the texture this response will hand back.
    explicit AvatarResponse(int edge = kDefaultEdge) : m_edge(edge) {}

    // The caller's sourceSize, clamped. A texture is cached per avatar URL for
    // as long as it is on screen, so an Image that asks for nothing gets the
    // small default rather than the full-size photo tacky ships.
    static int edgeFor(const QSize &requested) {
        const int asked = qMax(requested.width(), requested.height());
        return asked > 0 ? qMin(asked, kCeilingEdge) : kDefaultEdge;
    }

    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override { return m_error; }

    // AvatarSink, called on the GUI thread when the reply lands.
    void deliverBase64(const QByteArray &base64) override {
        const QByteArray bytes = QByteArray::fromBase64(base64);
        if (!bytes.isEmpty())
            m_image.loadFromData(bytes); // any format, often JPEG
        if (m_image.isNull()) {
            m_error = QStringLiteral("no avatar"); // -> Image.status == Error
        } else if (m_image.width() > m_edge || m_image.height() > m_edge) {
            // The backend ships full-size bytes; cache a texture the size it
            // is drawn at rather than a full-res photo per contact. A smaller
            // one is left alone - scaling it up here would only spend memory
            // to blur it sooner.
            m_image = m_image.scaled(m_edge, m_edge, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        }
        emit finished();
    }
    void failSink(const QString &reason) override {
        m_error = reason.isEmpty() ? QStringLiteral("no avatar") : reason;
        emit finished();
    }

private:
    static constexpr int kDefaultEdge = 96;  // a 44px avatar on a 2x display
    static constexpr int kCeilingEdge = 256; // the settings page's, on one too
    int m_edge;
    QImage m_image;
    QString m_error;
};

class AvatarImageProvider : public QQuickAsyncImageProvider {
public:
    explicit AvatarImageProvider(AvatarController *controller)
        : m_controller(controller) {}

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

private:
    AvatarController *m_controller;
};

#endif // AVATARIMAGEPROVIDER_H
