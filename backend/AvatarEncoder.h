// How the GUI hands AccountSettings a ready-to-publish avatar without this
// library knowing about QImage. tacky stores and sends `avatar publish -data`
// verbatim, so the crop, the scale and the encoding all happen on this side of
// the wire; the backend never resizes.
//
// Core only, like AvatarSink: the headless model tests link the encoder-less
// side and pass a stub.
#ifndef AVATARENCODER_H
#define AVATARENCODER_H

#include <QByteArray>
#include <QString>
#include <QUrl>

struct AvatarImage {
    QByteArray png;
    int width = 0;
    int height = 0;
};

class AvatarEncoder {
public:
    virtual ~AvatarEncoder() = default;

    // Empty png means it could not be used, with the reason in *error.
    virtual AvatarImage encode(const QUrl &source, QString *error) const = 0;
};

#endif // AVATARENCODER_H
