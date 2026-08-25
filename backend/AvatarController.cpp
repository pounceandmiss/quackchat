#include "AvatarController.h"

#include "AvatarSink.h"
#include "BackendBinding.h"
#include "TackyBackend.h"

AvatarController::AvatarController(QObject *parent) : QObject(parent) {}

void AvatarController::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (!m_backend)
        return;
    connect(m_backend, &TackyBackend::event, this, &AvatarController::handleEvent);
    connect(m_backend, &TackyBackend::result, this, &AvatarController::onResult);
    connect(m_backend, &TackyBackend::error, this, &AvatarController::onError);
    connect(m_backend, &TackyBackend::runningChanged, this,
            &AvatarController::onRunningChanged);
    connect(m_backend, &TackyBackend::connected, this, [this] {
        // Subscriptions are placed by hashFor(), which QML only re-runs when
        // rev moves; without a nudge nothing would ever ask again.
        ++m_rev;
        emit changed();
    });
}

void AvatarController::onRunningChanged() {
    if (m_backend && m_backend->isRunning())
        return;
    m_visible.clear(); // the backend holding them is gone
    const QHash<int, AvatarSink *> pending = m_pending;
    m_pending.clear();
    for (AvatarSink *sink : pending)
        sink->failSink(QStringLiteral("backend went away"));
}

QString AvatarController::key(const QString &acc, const QString &jid) {
    return acc + QLatin1Char('\n') + jidWithoutJoin(jid);
}

QString AvatarController::hashFor(const QString &acc, const QString &jid) {
    ensureVisible(acc, jid); // the read is what places the mark
    return m_hash.value(key(acc, jid));
}

// The mark is what asks: tacky re-emits an <Update> from its cache for every
// one, so a hash this frontend never heard about arrives the way a change does.
// An `avatar metadata` read beside it would answer from the metadata row alone,
// which can name a hash whose bytes are still missing.
//
// We never send `invisible`. A mark left standing only means tacky keeps that
// JID's avatar current, and reclaiming it isn't worth the bookkeeping yet.
void AvatarController::ensureVisible(const QString &acc, const QString &jid) {
    if (!m_backend || !m_backend->isRunning() || acc.isEmpty() || jid.isEmpty())
        return;
    const QString nj = jidWithoutJoin(jid);
    const QString k = key(acc, nj);
    if (m_visible.contains(k))
        return;
    m_visible.insert(k);
    m_backend->notify(QStringLiteral("avatar"), QStringLiteral("visible"),
                      QVariantMap{{QStringLiteral("acc"), acc},
                                  {QStringLiteral("jid"), nj}});
}

void AvatarController::refresh(const QString &acc, const QString &jid) {
    if (!m_backend || acc.isEmpty() || jid.isEmpty())
        return;
    m_backend->notify(QStringLiteral("avatar"), QStringLiteral("refresh"),
                      QVariantMap{{QStringLiteral("acc"), acc},
                                  {QStringLiteral("jid"), jidWithoutJoin(jid)}});
}

void AvatarController::fetch(const QString &acc, const QString &jid,
                             const QString &hash, AvatarSink *sink) {
    if (!m_backend || !m_backend->isRunning() || acc.isEmpty() ||
        hash.isEmpty()) {
        sink->failSink(QStringLiteral("no avatar"));
        return;
    }
    ensureVisible(acc, jid); // idempotent; keeps the JID pinned while shown

    // Content-addressed by hash; returns the published bytes in whatever format
    // and size they were, since the backend never resizes.
    const int tok =
        m_backend->request(QStringLiteral("avatar"), QStringLiteral("data"),
                           QVariantMap{{QStringLiteral("acc"), acc},
                                       {QStringLiteral("hash"), hash}});
    m_pending.insert(tok, sink);
}

void AvatarController::onResult(int token, const QVariant &data) {
    AvatarSink *sink = m_pending.take(token);
    if (!sink)
        return; // not one of ours
    // base64 image string. Every hash here came from an <Update>, which waits
    // for the bytes, so "" means the avatar went away mid-fetch - and the
    // <Update> saying so is already on its way.
    sink->deliverBase64(data.toString().toLatin1());
}

void AvatarController::onError(int token, const QString &message) {
    if (AvatarSink *sink = m_pending.take(token))
        sink->failSink(message);
}

void AvatarController::handleEvent(const QString &module, const QString &name,
                                   const QVariant &args) {
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    if (module != QLatin1String("avatar") || name != QLatin1String("Update"))
        return;
    const QVariantMap a = args.toMap();
    const QString jid = a.value(QStringLiteral("jid")).toString();
    if (jid.isEmpty())
        return;
    // A removal arrives as an empty hash.
    applyHash(key(a.value(QStringLiteral("acc")).toString(), jid),
              a.value(QStringLiteral("hash")).toString());
}

void AvatarController::applyHash(const QString &k, const QString &hash) {
    if (hash.isEmpty())
        m_hash.remove(k);
    else
        m_hash.insert(k, hash);
    ++m_rev;
    emit changed();
}
