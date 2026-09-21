#include "FrameStream.h"

#include <QtEndian>

Q_LOGGING_CATEGORY(lcVideo, "quack.video", QtWarningMsg)

namespace {

constexpr quint32 kMagic = 0x31465654u;   // 'T','V','F','1'
constexpr quint16 kVersion = 1;
constexpr int     kHeaderBytes = 40;
constexpr quint32 kFormatI420 = 0x30323449u;
constexpr quint32 kMaxPayload = 64u * 1024 * 1024;
constexpr quint32 kMaxSide = 16384;

qsizetype i420Bytes(quint32 w, quint32 h)
{
    return qsizetype(w) * h + 2 * (qsizetype((w + 1) / 2) * ((h + 1) / 2));
}

} // namespace

FrameStream::FrameStream(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::readyRead, this, &FrameStream::onReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected, this, &FrameStream::ended);
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError e) {
        if (e == QLocalSocket::PeerClosedError)
            return; // disconnected covers it
        qCWarning(lcVideo) << "frame stream" << m_name << ":" << m_socket.errorString();
        emit ended();
    });
}

void FrameStream::open(const QString &name)
{
    close();
    m_name = name;
    // Read-only: the writer's end is outbound only, and on Windows asking
    // for write access to it is refused.
    m_socket.connectToServer(name, QIODevice::ReadOnly);
}

void FrameStream::close()
{
    const QSignalBlocker quiet(&m_socket); // no ended() for our own close
    m_socket.abort();
    m_buf.clear();
    m_latest = Frame();
}

void FrameStream::onReadyRead()
{
    m_buf.append(m_socket.readAll());
    bool gotFrame = false;
    if (!parse(gotFrame)) {
        qCWarning(lcVideo) << "frame stream" << m_name << "sent a malformed header";
        close();
        emit ended();
        return;
    }
    if (gotFrame)
        emit frameReady();
}

bool FrameStream::parse(bool &gotFrame)
{
    qsizetype off = 0;
    while (m_buf.size() - off >= kHeaderBytes) {
        const auto *h = reinterpret_cast<const uchar *>(m_buf.constData() + off);
        const quint16 hlen = qFromLittleEndian<quint16>(h + 6);
        const quint32 w = qFromLittleEndian<quint32>(h + 16);
        const quint32 ht = qFromLittleEndian<quint32>(h + 20);
        const quint32 plen = qFromLittleEndian<quint32>(h + 24);
        if (qFromLittleEndian<quint32>(h) != kMagic || qFromLittleEndian<quint16>(h + 4) != kVersion
            || hlen < kHeaderBytes || qFromLittleEndian<quint32>(h + 8) != kFormatI420
            || w == 0 || ht == 0 || w > kMaxSide || ht > kMaxSide || plen > kMaxPayload
            || qsizetype(plen) != i420Bytes(w, ht))
            return false;
        if (m_buf.size() - off < hlen + qsizetype(plen))
            break;
        m_latest.width = int(w);
        m_latest.height = int(ht);
        m_latest.keyframe = qFromLittleEndian<quint32>(h + 12) & 1u;
        m_latest.ptsNs = qFromLittleEndian<qint64>(h + 32);
        m_latest.planes = m_buf.mid(off + hlen, plen);
        off += hlen + plen;
        gotFrame = true;
    }
    m_buf.remove(0, off);
    return true;
}
