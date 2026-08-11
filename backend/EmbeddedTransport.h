// libtacky linked into this process. The Tcl interpreter runs on its own thread
// and the emit callback fires there, so every frame is hopped onto the thread
// owning this object before it is signalled.
#ifndef EMBEDDEDTRANSPORT_H
#define EMBEDDEDTRANSPORT_H

#include "TackyTransport.h"

extern "C" {
struct tacky; // opaque backend handle (embed/tacky.h)
}

class EmbeddedTransport : public TackyTransport {
    Q_OBJECT

public:
    explicit EmbeddedTransport(QObject *parent = nullptr);
    ~EmbeddedTransport() override;

    // tacoArgs go to the taco_type constructor, e.g. {"-transient","0"}; empty
    // means an in-memory session. Blocks until ready.
    bool start(const QStringList &tacoArgs) override;
    void stop() override;
    bool isConnected() const override { return m_client != nullptr; }
    void send(const QByteArray &json) override;

private:
    // Runs on the backend thread; must not block or re-enter tacky.
    static void emitTrampoline(void *ud, const char *json, size_t len);

    tacky *m_client = nullptr;
};

#endif // EMBEDDEDTRANSPORT_H
