// tacky reached over a socket, for Android: the backend service owns the
// interpreter in a process that survives the activity, and this end reconnects
// to it. Framing is tacky's own lenpipe (see lib/lenpipe/lenpipe.tcl):
//
//     <byte_length>\n<utf8_payload>     repeating, no separator after payload
//
// Nothing is queued while disconnected. Callers re-sync on connectedChanged
// instead, which is what the models' <Ready>/refresh paths already do.
#ifndef SOCKETTRANSPORT_H
#define SOCKETTRANSPORT_H

#include <QLocalSocket>
#include <QTimer>

#include "TackyTransport.h"

class SocketTransport : public TackyTransport {
    Q_OBJECT

public:
    // `serverName` is an abstract-namespace name: that is what Android's
    // LocalServerSocket binds, and binding a filesystem path from Java needs a
    // raw FileDescriptor, which java.nio only grew at API 34.
    explicit SocketTransport(const QString &serverName, QObject *parent = nullptr);
    ~SocketTransport() override;

    // tacoArgs are ignored: the session belongs to whoever owns the socket.
    // Returns true once the first connection attempt is under way.
    bool start(const QStringList &tacoArgs) override;
    void stop() override;
    bool isConnected() const override;
    void send(const QByteArray &json) override;

    // How long a dropped link waits before reconnecting; only tests care, and
    // only so they need not sit out a whole second.
    void setRetryInterval(int ms) { m_retryMs = ms; }

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();

private:
    // Ceiling on one frame. Well above the largest real payload (an unresized
    // avatar as base64, or a whole chatlist), and low enough that a desynced
    // length cannot be turned into an allocation.
    static constexpr qint64 kMaxFrame = 64 * 1024 * 1024;

    void connectToService();
    void resetFraming();
    void reportState();

    QLocalSocket m_sock;
    QTimer m_retry;
    QString m_name;
    bool m_wanted = false;   // start() called and stop() not yet
    bool m_reported = false; // last state handed to connectedChanged
    int m_retryMs = 1000;

    QByteArray m_buf;
    qint64 m_expected = -1; // -1 while the length line is still incomplete
};

#endif // SOCKETTRANSPORT_H
