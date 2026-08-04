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
        } else if (m_image.width() > kMaxEdge || m_image.height() > kMaxEdge) {
            // The backend ships full-size bytes; cache a small texture rather
            // than a full-res photo per contact.
            m_image = m_image.scaled(kMaxEdge, kMaxEdge, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        }
        emit finished();
    }
    void failSink(const QString &reason) override {
        m_error = reason.isEmpty() ? QStringLiteral("no avatar") : reason;
        emit finished();
    }

private:
    static constexpr int kMaxEdge = 96; // covers a 44px avatar on a 2x display
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
