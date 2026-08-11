#include "EmbeddedTransport.h"

#include <QMetaObject>

#include <vector>

#include "tacky.h"

EmbeddedTransport::EmbeddedTransport(QObject *parent) : TackyTransport(parent) {}

EmbeddedTransport::~EmbeddedTransport() { stop(); }

void EmbeddedTransport::emitTrampoline(void *ud, const char *json, size_t len) {
    auto *self = static_cast<EmbeddedTransport *>(ud);
    // Copy out of the transient buffer, then hop to the owning thread: the
    // functor form of invokeMethod delivers on `self`'s thread.
    QString s = QString::fromUtf8(json, static_cast<int>(len));
    QMetaObject::invokeMethod(
        self, [self, s]() { emit self->received(s); }, Qt::QueuedConnection);
}

bool EmbeddedTransport::start(const QStringList &tacoArgs) {
    if (m_client)
        return true;

    // Hold the UTF-8 bytes alive across the call (tacky_create strdups them).
    QList<QByteArray> holder;
    holder.reserve(tacoArgs.size());
    std::vector<const char *> argv;
    argv.reserve(tacoArgs.size() + 1);
    for (const QString &a : tacoArgs) {
        holder.append(a.toUtf8());
        argv.push_back(holder.last().constData());
    }
    argv.push_back(nullptr);

    m_client = tacky_create(argv.data(), &EmbeddedTransport::emitTrampoline, this);
    if (m_client)
        emit connectedChanged();
    return m_client != nullptr;
}

void EmbeddedTransport::stop() {
    if (!m_client)
        return;
    tacky *c = m_client;
    m_client = nullptr;
    tacky_destroy(c); // no callbacks fire after this returns
    emit connectedChanged();
}

void EmbeddedTransport::send(const QByteArray &json) {
    if (!m_client)
        return;
    tacky_send(m_client, json.constData(), static_cast<size_t>(json.size()));
}
