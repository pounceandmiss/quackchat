// Reader for an rtc-mv frame stream, connected to by the name a calls
// <VideoTrack>/<VideoPreview> event carries: a Unix socket path, or a named
// pipe on Windows. Frames are a 40-byte header and tight I420 planes; the
// layout is in tacky's doc/DOC.md. frameReady fires once per batch of
// arrivals, and latest() is the newest complete frame.

#pragma once

#include <QByteArray>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QObject>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcVideo)

class FrameStream : public QObject {
    Q_OBJECT
public:
    struct Frame {
        qint64 ptsNs = 0;
        int    width = 0;
        int    height = 0;
        bool   keyframe = false;
        // Tightly packed I420: Y (w*h), U and V ((w+1)/2 * (h+1)/2 each).
        QByteArray planes;
    };

    explicit FrameStream(QObject *parent = nullptr);

    void open(const QString &name);
    void close();
    bool isOpen() const { return m_socket.state() != QLocalSocket::UnconnectedState; }

    // Only valid after frameReady.
    const Frame &latest() const { return m_latest; }

signals:
    void frameReady();
    // The writer went away, or sent something that isn't a frame stream.
    void ended();

private:
    void onReadyRead();
    // Consumes whole frames from m_buf; false on a malformed header.
    bool parse(bool &gotFrame);

    QLocalSocket m_socket;
    QString      m_name;
    QByteArray   m_buf;
    Frame        m_latest;
};
