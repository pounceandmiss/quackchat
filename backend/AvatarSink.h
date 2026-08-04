// How AvatarController hands a fetched avatar back without knowing about
// QImage/QtQuick. The image provider's response implements it; keeping it
// abstract is what lets the controller stay a pure Core/Qml type.
#ifndef AVATARSINK_H
#define AVATARSINK_H

#include <QByteArray>
#include <QString>

class AvatarSink {
public:
    virtual ~AvatarSink() = default;
    // Base64 bytes as tacky returned them; "" means no avatar.
    virtual void deliverBase64(const QByteArray &base64) = 0;
    virtual void failSink(const QString &reason) = 0;
};

#endif // AVATARSINK_H
