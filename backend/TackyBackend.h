// Qt bridge to libtacky. It runs a Tcl interpreter on its own thread and its
// emit callback fires there, so we hop every message onto the thread owning
// this object; the signals below are all emitted there. QtCore only.
#ifndef TACKYBACKEND_H
#define TACKYBACKEND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

extern "C" {
struct tacky; // opaque backend handle (embed/tacky.h)
}

class TackyBackend : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("TackyBackend is created and started in C++.")
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit TackyBackend(QObject *parent = nullptr);
    ~TackyBackend() override;

    bool isRunning() const { return m_client != nullptr; }

    // tacoArgs go to the taco_type constructor, e.g. {"-transient","0"}; empty
    // means an in-memory session. Blocks until ready.
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
    // Every request/notify, whether or not a backend is running to receive it.
    void sent(const QString &module, const QString &method, const QVariant &args);
    void result(int token, const QVariant &data);
    void error(int token, const QString &message);
    void event(const QString &module, const QString &name, const QVariant &args);
    void rawMessage(const QString &json); // every emit, before decoding

private:
    // Runs on the backend thread; must not block or re-enter tacky.
    static void emitTrampoline(void *ud, const char *json, size_t len);
    void deliver(const QString &json);
    void sendArray(const QString &module, const QString &method,
                   const QVariant &args, bool withToken, int token);

    tacky *m_client = nullptr;
    int m_nextToken = 1;
};

#endif // TACKYBACKEND_H
