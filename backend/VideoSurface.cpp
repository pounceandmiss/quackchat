#include "VideoSurface.h"

#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <cstring>

namespace {
constexpr qint64 kStallMs = 3000;
}

VideoSurface::VideoSurface(QObject *parent) : QObject(parent)
{
    m_stallTimer.setSingleShot(true);
    m_stallTimer.setInterval(kStallMs);
    connect(&m_stallTimer, &QTimer::timeout, this, &VideoSurface::stalled);
    connect(&m_stream, &FrameStream::frameReady, this, &VideoSurface::deliver);
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
    if (name == m_openName && m_stream.isOpen())
        return;

    m_stream.close();
    m_openName = name;
    m_stallTimer.stop();
    if (m_hasFrame) {
        m_hasFrame = false;
        emit hasFrameChanged();
    }
    if (m_sink)
        m_sink->setVideoFrame(QVideoFrame());

    m_stalled = false;
    m_w = 0;
    m_h = 0;

    if (name.isEmpty())
        return;
    m_stream.open(name);
    m_since.start();
    m_stallTimer.start();
}

// The stream is open and nothing is coming through it: the line a
// "no video" report needs.
void VideoSurface::stalled()
{
    m_stalled = true;
    qCWarning(lcVideo) << "no frames on" << m_openName << "for" << m_since.elapsed()
                       << "ms;" << (m_hasFrame ? "stopped" : "none ever arrived");
}

void VideoSurface::deliver()
{
    m_since.restart();
    m_stallTimer.start();
    if (!m_sink)
        return;
    const FrameStream::Frame &f = m_stream.latest();
    if (m_stalled) {
        m_stalled = false;
        qCWarning(lcVideo) << "frames resumed on" << m_openName;
    }

    const int w = f.width, h = f.height;
    const int cw = (w + 1) / 2, ch = (h + 1) / 2;
    if (w != m_w || h != m_h) {
        m_w = w;
        m_h = h;
        qCDebug(lcVideo) << "frame" << w << "x" << h << "on" << m_openName;
    }

    const qsizetype ys = qsizetype(w) * h;
    const qsizetype cs = qsizetype(cw) * ch;
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
