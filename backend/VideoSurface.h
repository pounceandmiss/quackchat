// Feeds a video frame stream into a QVideoSink so QML can render it with a
// stock VideoOutput. Give it the channel arg-map from a calls
// <VideoTrack>/<VideoPreview> event and bind videoSink to the VideoOutput.

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

#include "FrameStream.h"

class VideoSurface : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    // The <VideoTrack>/<VideoPreview> event args (needs at least "name").
    Q_PROPERTY(QVariantMap channel READ channel WRITE setChannel NOTIFY channelChanged)
    // True once at least one frame has been delivered.
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)

public:
    explicit VideoSurface(QObject *parent = nullptr);
    ~VideoSurface() override;

    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);

    QVariantMap channel() const { return m_channel; }
    void setChannel(const QVariantMap &c);

    bool hasFrame() const { return m_hasFrame; }

signals:
    void videoSinkChanged();
    void channelChanged();
    void hasFrameChanged();

private:
    void reopen();
    void deliver();
    void stalled();

    QVideoSink  *m_sink = nullptr;
    QVariantMap  m_channel;
    bool         m_hasFrame = false;

    FrameStream   m_stream;
    QString       m_openName;
    // Re-armed by every frame: firing means nothing arrived for a while.
    QTimer        m_stallTimer;
    // Since the last frame, or since the stream opened.
    QElapsedTimer m_since;
    bool          m_stalled = false;
    int           m_w = 0;
    int           m_h = 0;
};
