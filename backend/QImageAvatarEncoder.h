// The QImage half of avatar publishing: decode whatever the picker returned,
// centre-crop it square, scale it, and PNG-encode. Kept out of the `quack`
// library so the headless tests never link QtGui - the same split
// AvatarSink/AvatarImageProvider already use.
#ifndef QIMAGEAVATARENCODER_H
#define QIMAGEAVATARENCODER_H

#include "AvatarEncoder.h"

#include <QCoreApplication>

class QImageAvatarEncoder : public AvatarEncoder {
    // No QObject to inherit tr() from, and the two failures it reports are read
    // by whoever tried to publish a picture.
    Q_DECLARE_TR_FUNCTIONS(QImageAvatarEncoder)

public:
    // Matches tacky's own client. The published PNG is the exact blob every
    // subscriber downloads, so it has to stay under the server's stanza cap;
    // 128px of photo is ~30-70KB base64, comfortable on typical servers.
    static constexpr int kEdge = 128;

    AvatarImage encode(const QUrl &source, QString *error) const override;
};

#endif // QIMAGEAVATARENCODER_H
