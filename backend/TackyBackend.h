// Qt bridge to tacky, over whichever TackyTransport it was given. Frames arrive
// on the thread owning this object, so the signals below are all emitted there.
// QtCore only.
#ifndef TACKYBACKEND_H
#define TACKYBACKEND_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class TackyTransport;

class TackyBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TackyBackend is created and started in C++.")
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit TackyBackend(QObject *parent = nullptr);
    ~TackyBackend() override;

    bool isRunning() const;

    // Hands the backend a transport to run over, replacing any previous one.
    // Takes ownership. Call before start(); without it start() builds an
    // EmbeddedTransport, which is what the desktop wants.
    void setTransport(TackyTransport *transport);

    // tacoArgs go to the taco_type constructor, e.g. {"-transient","0"}; empty
    // means an in-memory session. They reach an embedded transport only: with a
    // socket the session belongs to whoever is on the other end.
    Q_INVOKABLE bool start(const QStringList &tacoArgs = {});
    Q_INVOKABLE void stop(); // safe to call more than once

    // Sends ["module","method",{args},token]; the reply arrives later as
    // result(token,...) or error(token,...).
    Q_INVOKABLE int request(const QString &module, const QString &method,
                            const QVariant &args = QVariantMap());

    // Fire-and-forget: no token, no reply.
    Q_INVOKABLE void notify(const QString &module, const QString &method,
                            const QVariant &args = QVariantMap());

signals:
    void runningChanged();
    // Rising edge of runningChanged. State the backend held for us is gone by
    // now, so this is where models re-ask for what they were showing.
    void connected();
    // Every request/notify, whether or not a backend is running to receive it.
    void sent(const QString &module, const QString &method, const QVariant &args);
    void result(int token, const QVariant &data);
    void error(int token, const QString &message);
    void event(const QString &module, const QString &name, const QVariant &args);
    void rawMessage(const QString &json); // every emit, before decoding

private:
    void deliver(const QString &json);
    void onTransportStateChanged();
    void sendArray(const QString &module, const QString &method,
                   const QVariant &args, bool withToken, int token);

    TackyTransport *m_transport = nullptr; // owned
    QHash<int, QString> m_inflight;        // token -> "module/method", for errors
    int m_nextToken = 1;
};

#endif // TACKYBACKEND_H
