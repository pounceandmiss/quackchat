// Feeds the video ring into a QVideoSink so QML can render it with a
// stock VideoOutput. Give it the channel arg-map from a calls
// <VideoTrack>/<VideoPreview> event and bind videoSink to the VideoOutput.

#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVideoSink>
#include <QtQml/qqmlregistration.h>

#include "FrameChannel.h"

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

private slots:
    void poll();

private:
    void reopen();

    QVideoSink  *m_sink = nullptr;
    QVariantMap  m_channel;
    bool         m_hasFrame = false;

    FrameChannel m_ring;
    QString      m_openName;
    QTimer       m_timer;
};
