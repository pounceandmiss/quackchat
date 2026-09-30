// The permission prompts a call goes through before it goes out, shared by the
// 1:1 and the group calls. Only Android gates the mic here: desktop Linux has
// no permission backend for it, and asking there would answer Undetermined
// forever.
#ifndef MEDIAPERMISSIONS_H
#define MEDIAPERMISSIONS_H

#include <QCoreApplication>
#include <QObject>
#include <QPermissions>

#include <functional>

namespace media {

// Runs `then` once the mic is ours to use, `denied` if it is not. `ctx` is the
// receiver the prompt's answer is delivered to, so it dies with it.
inline void withMicrophone(QObject *ctx, const std::function<void()> &then,
                           const std::function<void()> &denied) {
#ifdef Q_OS_ANDROID
    const QMicrophonePermission perm;
    switch (qApp->checkPermission(perm)) {
    case Qt::PermissionStatus::Granted:
        then();
        return;
    case Qt::PermissionStatus::Denied:
        denied();
        return;
    case Qt::PermissionStatus::Undetermined:
        // The prompt is modal to the user but async to us, so the call only
        // goes out once they have answered.
        qApp->requestPermission(perm, ctx, [then, denied](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted)
                then();
            else
                denied();
        });
        return;
    }
#else
    Q_UNUSED(ctx)
    Q_UNUSED(denied)
    then();
#endif
}

// Asks for the camera when `video`; `then` runs either way, since a call
// without a picture is still a call.
inline void withCamera(QObject *ctx, bool video, const std::function<void()> &then) {
#ifdef Q_OS_ANDROID
    const QCameraPermission perm;
    if (video && qApp->checkPermission(perm) == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(perm, ctx, [then](const QPermission &) { then(); });
        return;
    }
#else
    Q_UNUSED(ctx)
    Q_UNUSED(video)
#endif
    then();
}

} // namespace media

#endif // MEDIAPERMISSIONS_H
