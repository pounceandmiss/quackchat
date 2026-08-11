#include "SocketTransport.h"

SocketTransport::SocketTransport(const QString &serverName, QObject *parent)
    : TackyTransport(parent), m_name(serverName) {
    m_sock.setSocketOptions(QLocalSocket::AbstractNamespaceOption);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &SocketTransport::connectToService);
    connect(&m_sock, &QLocalSocket::connected, this, &SocketTransport::onConnected);
    connect(&m_sock, &QLocalSocket::disconnected, this,
            &SocketTransport::onDisconnected);
    connect(&m_sock, &QLocalSocket::errorOccurred, this,
            &SocketTransport::onDisconnected);
    connect(&m_sock, &QLocalSocket::readyRead, this, &SocketTransport::onReadyRead);
}

// Members die before ~QObject drops the connections to them, and ~QLocalSocket
// emits disconnected on its way out - which would land in onDisconnected and
// touch an m_retry that no longer exists. Cut the wiring first.
SocketTransport::~SocketTransport() {
    m_sock.disconnect(this);
    m_wanted = false;
    m_retry.stop();
    m_sock.abort();
}

bool SocketTransport::start(const QStringList &) {
    if (m_wanted)
        return true;
    m_wanted = true;
    connectToService();
    return true;
}

void SocketTransport::stop() {
    if (!m_wanted)
        return;
    m_wanted = false;
    m_retry.stop();
    m_sock.abort();
    resetFraming();
    reportState();
}

bool SocketTransport::isConnected() const {
    return m_sock.state() == QLocalSocket::ConnectedState;
}

void SocketTransport::connectToService() {
    if (!m_wanted || m_sock.state() != QLocalSocket::UnconnectedState)
        return;
    m_sock.connectToServer(m_name);
}

void SocketTransport::onConnected() {
    resetFraming();
    reportState();
}

// errorOccurred lands here too: a refused connection needs the same retry as a
// dropped one.
void SocketTransport::onDisconnected() {
    resetFraming();
    reportState();
    if (m_wanted && !m_retry.isActive())
        m_retry.start(m_retryMs);
}

// Only real transitions reach the backend; errorOccurred can fire repeatedly
// while already unconnected.
void SocketTransport::reportState() {
    const bool now = isConnected();
    if (now == m_reported)
        return;
    m_reported = now;
    emit connectedChanged();
}

void SocketTransport::resetFraming() {
    m_buf.clear();
    m_expected = -1;
}

void SocketTransport::onReadyRead() {
    m_buf.append(m_sock.readAll());
    forever {
        if (m_expected < 0) {
            const int nl = m_buf.indexOf('\n');
            if (nl < 0)
                return; // length line still incomplete
            bool ok = false;
            m_expected = m_buf.left(nl).trimmed().toLongLong(&ok);
            // A length that is unparseable or absurd means the stream is out of
            // step. There is no resynchronisation point in this framing, and
            // trusting the number would size an allocation from it, so drop the
            // link and let the retry start clean.
            if (!ok || m_expected < 0 || m_expected > kMaxFrame) {
                resetFraming();
                m_sock.abort();
                return;
            }
            m_buf.remove(0, nl + 1);
        }
        if (m_buf.size() < m_expected)
            return; // payload still arriving
        const QByteArray payload = m_buf.left(m_expected);
        m_buf.remove(0, m_expected);
        m_expected = -1;
        emit received(QString::fromUtf8(payload));
    }
}

void SocketTransport::send(const QByteArray &json) {
    if (!isConnected())
        return;
    m_sock.write(QByteArray::number(json.size()) + '\n' + json);
    // Requests are latency-sensitive and often issued in bursts from one call
    // stack; without this they would all sit until the next event loop turn.
    m_sock.flush();
}
