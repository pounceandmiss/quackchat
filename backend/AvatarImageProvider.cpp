#include "AvatarImageProvider.h"

#include <QMetaObject>

#include "AvatarController.h"

QQuickImageResponse *
AvatarImageProvider::requestImageResponse(const QString &id,
                                          const QSize &requestedSize) {
    auto *resp = new AvatarResponse(AvatarResponse::edgeFor(requestedSize));

    // id is "<acc>/<jid>/<hash>". acc is a bare JID and hash is hex, so neither
    // holds a '/'; splitting on the first and last one survives a jid that does
    // (a MUC occupant's room@muc/nick).
    const int first = id.indexOf(QLatin1Char('/'));
    const int last = id.lastIndexOf(QLatin1Char('/'));
    if (first < 0 || last <= first) {
        resp->failSink(QStringLiteral("bad avatar id"));
        return resp;
    }
    const QString acc = id.left(first);
    const QString jid = id.mid(first + 1, last - first - 1);
    const QString hash = id.mid(last + 1);

    // We are on QML's image-loader thread; the backend lives on the
    // controller's, so run the fetch there.
    AvatarController *c = m_controller;
    QMetaObject::invokeMethod(
        c, [c, acc, jid, hash, resp]() { c->fetch(acc, jid, hash, resp); },
        Qt::QueuedConnection);
    return resp;
}
