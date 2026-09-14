#include "VideoSurface.h"

#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cstring>

VideoSurface::VideoSurface(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(33); // ~30 Hz
    connect(&m_timer, &QTimer::timeout, this, &VideoSurface::poll);
}

VideoSurface::~VideoSurface() = default;

void VideoSurface::setVideoSink(QVideoSink *sink)
{
    if (m_sink == sink)
        return;
    m_sink = sink;
    emit videoSinkChanged();
}

void VideoSurface::setChannel(const QVariantMap &c)
{
    if (m_channel == c)
        return;
    m_channel = c;
    emit channelChanged();
    reopen();
}

void VideoSurface::reopen()
{
    const QString name = m_channel.value(QStringLiteral("name")).toString();
    if (name == m_openName && m_ring.isOpen())
        return;

    m_ring.close();
    m_openName = name;
    m_timer.stop();
    if (m_hasFrame) {
        m_hasFrame = false;
        emit hasFrameChanged();
    }
    if (m_sink)
        m_sink->setVideoFrame(QVideoFrame());

    if (name.isEmpty())
        return;
    if (m_ring.openByName(name))
        m_timer.start();
}

void VideoSurface::poll()
{
    if (!m_ring.isOpen() || !m_sink)
        return;

    FrameChannel::Frame f;
    bool got = false;
    while (m_ring.read(f)) // drain to the newest this tick
        got = true;
    if (!got)
        return;

    const int w = f.width, h = f.height;
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    const qsizetype ys = qsizetype(w) * h;
    const qsizetype cs = qsizetype(cw) * ch;
    if (f.planes.size() < ys + 2 * cs)
        return;

    QVideoFrameFormat fmt(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
    QVideoFrame frame(fmt);
    if (!frame.map(QVideoFrame::WriteOnly))
        return;

    const auto *src = reinterpret_cast<const uchar *>(f.planes.constData());
    const uchar *sy = src;
    const uchar *su = src + ys;
    const uchar *sv = src + ys + cs;

    auto copyPlane = [](uchar *dst, int dstStride, const uchar *s, int sStride,
                        int rows, int rowBytes) {
        if (dstStride == sStride && dstStride == rowBytes) {
            std::memcpy(dst, s, qsizetype(rowBytes) * rows);
            return;
        }
        for (int r = 0; r < rows; ++r)
            std::memcpy(dst + qsizetype(r) * dstStride, s + qsizetype(r) * sStride,
                        rowBytes);
    };

    copyPlane(frame.bits(0), frame.bytesPerLine(0), sy, w,  h,  w);
    copyPlane(frame.bits(1), frame.bytesPerLine(1), su, cw, ch, cw);
    copyPlane(frame.bits(2), frame.bytesPerLine(2), sv, cw, ch, cw);
    frame.unmap();

    m_sink->setVideoFrame(frame);

    if (!m_hasFrame) {
        m_hasFrame = true;
        emit hasFrameChanged();
    }
}
