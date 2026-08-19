#include "AvatarController.h"

#include "AvatarSink.h"
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
    m_metaPending.clear();
    const QHash<int, Fetch> pending = m_pending;
    m_pending.clear();
    for (const Fetch &f : pending)
        f.sink->failSink(QStringLiteral("backend went away"));
}

QString AvatarController::key(const QString &acc, const QString &jid) {
    return acc + QLatin1Char('\n') + normJid(jid);
}

// Match tacky's avatarcache: the ?join suffix is not part of the JID an avatar
// lives under. The resource is kept, so MUC occupants stay distinct.
QString AvatarController::normJid(const QString &jid) {
    if (jid.endsWith(QLatin1String("?join")))
        return jid.left(jid.size() - 5);
    return jid;
}

QString AvatarController::hashFor(const QString &acc, const QString &jid) {
    ensureVisible(acc, jid); // the read is what starts the fetch
    return m_hash.value(key(acc, jid));
}

// `visible` subscribes, and primes the hash from tacky's cache - but only for a
// JID nobody had marked yet, and those marks outlive this process. A re-mark is
// then a no-op with no <Update> behind it, so the `metadata` read beside it is
// what answers: a cache lookup, no mark, no network.
//
// We never send `invisible`. A mark left standing only means tacky keeps that
// JID's avatar current, and reclaiming it isn't worth the bookkeeping yet.
void AvatarController::ensureVisible(const QString &acc, const QString &jid) {
    if (!m_backend || !m_backend->isRunning() || acc.isEmpty() || jid.isEmpty())
        return;
    const QString nj = normJid(jid);
    const QString k = key(acc, nj);
    if (m_visible.contains(k))
        return;
    m_visible.insert(k);
    const QVariantMap args{{QStringLiteral("acc"), acc},
                           {QStringLiteral("jid"), nj}};
    m_backend->notify(QStringLiteral("avatar"), QStringLiteral("visible"), args);
    const int tok = m_backend->request(QStringLiteral("avatar"),
                                       QStringLiteral("metadata"), args);
    m_metaPending.insert(tok, k);
}

void AvatarController::refresh(const QString &acc, const QString &jid) {
    if (!m_backend || acc.isEmpty() || jid.isEmpty())
        return;
    m_backend->notify(QStringLiteral("avatar"), QStringLiteral("refresh"),
                      QVariantMap{{QStringLiteral("acc"), acc},
                                  {QStringLiteral("jid"), normJid(jid)}});
}

void AvatarController::resubscribe(const QString &acc) {
    if (acc.isEmpty())
        return;
    const QString prefix = acc + QLatin1Char('\n');
    QStringList jids;
    for (auto it = m_visible.begin(); it != m_visible.end();) {
        if (it->startsWith(prefix)) {
            jids.append(it->mid(prefix.size()));
            it = m_visible.erase(it); // ensureVisible re-inserts
        } else {
            ++it;
        }
    }
    for (const QString &jid : jids)
        ensureVisible(acc, jid);
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
    m_pending.insert(tok, Fetch{sink, key(acc, jid), hash});
}

void AvatarController::onResult(int token, const QVariant &data) {
    if (const QString k = m_metaPending.take(token); !k.isNull()) {
        // An empty reply is "nothing cached", not the removal an <Update>
        // carries: it must not unset a hash we already know.
        const QString hash =
            data.toMap().value(QStringLiteral("hash")).toString();
        if (!hash.isEmpty())
            applyHash(k, hash);
        return;
    }
    const Fetch f = m_pending.take(token);
    if (!f.sink)
        return; // not one of ours
    // base64 image string, "" when the bytes aren't cached
    const QByteArray base64 = data.toString().toLatin1();
    f.sink->deliverBase64(base64);
    // An `avatar metadata` read answers from the metadata row alone, so it can
    // name a hash whose bytes tacky has not fetched yet - every <Update> route
    // waits for the bytes, that one does not. Forget such a hash rather than
    // leave QML pointing an Image at bytes that aren't there: the URL retracts
    // to the initials, and the <Update> the backend's own fetch ends in
    // re-forms it. Keeping it would leave the same URL the Image has already
    // failed on, which is one it never reloads. A hash that moved while we were
    // waiting is someone else's news, so only the one we asked for is dropped.
    if (base64.isEmpty() && m_hash.value(f.key) == f.hash)
        applyHash(f.key, QString());
}

void AvatarController::onError(int token, const QString &message) {
    if (m_metaPending.remove(token))
        return; // no hash to record; the <Update> route still stands
    const Fetch f = m_pending.take(token);
    if (!f.sink)
        return;
    f.sink->failSink(message);
}

void AvatarController::handleEvent(const QString &module, const QString &name,
                                   const QVariant &args) {
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    if (module == QLatin1String("conn") && name == QLatin1String("Ready")) {
        resubscribe(args.toMap().value(QStringLiteral("acc")).toString());
        return;
    }
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
