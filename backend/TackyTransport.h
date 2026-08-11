// How TackyBackend reaches tacky. Desktop links the interpreter in-process
// (EmbeddedTransport); Android talks to the backend service over a socket
// (SocketTransport), because Qt kills the UI process on activity destroy and
// the connection has to outlive it. QtCore only at this level.
#ifndef TACKYTRANSPORT_H
#define TACKYTRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

class TackyTransport : public QObject {
    Q_OBJECT

public:
    explicit TackyTransport(QObject *parent = nullptr) : QObject(parent) {}

    // False means the transport could not even be set up. A transport that
    // connects asynchronously returns true and reports it via connectedChanged.
    virtual bool start(const QStringList &tacoArgs) = 0;
    virtual void stop() = 0;
    virtual bool isConnected() const = 0;

    // One complete JSON frame. Dropped when not connected: callers re-sync on
    // the next connected edge rather than queueing behind a dead link.
    virtual void send(const QByteArray &json) = 0;

signals:
    void connectedChanged();
    void received(const QString &json);
};

#endif // TACKYTRANSPORT_H
