#include "OmemoChat.h"

#include "BackendBinding.h"

OmemoChat::OmemoChat(QObject *parent) : QObject(parent) {}

void OmemoChat::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &OmemoChat::refresh);
    emit backendChanged();
    refresh();
}

void OmemoChat::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    const bool was = available();
    m_account = acc;
    emit accountChanged();
    if (was != available())
        emit availableChanged();
    refresh();
}

void OmemoChat::setJid(const QString &jid) {
    if (m_jid == jid)
        return;
    const bool was = available();
    m_jid = jid;
    emit jidChanged();
    if (was != available())
        emit availableChanged();
    refresh();
}

void OmemoChat::setGroupchat(bool v) {
    if (m_groupchat == v)
        return;
    const bool was = available();
    m_groupchat = v;
    emit groupchatChanged();
    if (was != available())
        emit availableChanged();
    refresh();
}

// The answer on screen belongs to the chat we just left, and one chat's "off"
// showing over another's composer would misreport how the next message goes
// out. Back to knowing nothing until this chat's own answer lands.
void OmemoChat::refresh() {
    setKnown(false);
    m_readToken = -1;
    m_prepared = false;
    if (!m_backend || !available())
        return;
    m_readToken = m_backend->request(
        QStringLiteral("omemo"), QStringLiteral("isEnabled"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("jid"), m_jid}});
}

void OmemoChat::handleResult(int token, const QVariant &data) {
    if (token != m_readToken)
        return;
    m_readToken = -1;
    applyEnabled(data.toBool());
    setKnown(true);
    if (m_enabled)
        prepare();
}

// Left unknown rather than guessed at: the next connected edge asks again, and
// until then there is nothing to draw.
void OmemoChat::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token != m_readToken)
        return;
    m_readToken = -1;
}

void OmemoChat::setKnown(bool v) {
    if (m_known == v)
        return;
    m_known = v;
    emit knownChanged();
}

void OmemoChat::setEnabled(bool on) {
    if (!available() || m_enabled == on)
        return;
    // Optimistic, as with blind trust: the padlock follows the click and
    // <Enabled> confirms it. The switch is only offered once the state is
    // known, so this settles it rather than guessing at it.
    applyEnabled(on);
    setKnown(true);
    if (!m_backend)
        return;
    m_backend->notify(QStringLiteral("omemo"), QStringLiteral("setEnabled"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jid"), m_jid},
                                  {QStringLiteral("value"), on ? 1 : 0}});
}

void OmemoChat::handleEvent(const QString &module, const QString &name,
                            const QVariant &args) {
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account || m_account.isEmpty())
        return;
    if (module == QLatin1String("omemo") && name == QLatin1String("Enabled")) {
        // Scoped to this chat as well: every open chat sees every account
        // event, and a pull answered after a switch is about the old one.
        if (a.value(QStringLiteral("jid")).toString() != m_jid)
            return;
        const bool on = a.value(QStringLiteral("value")).toBool();
        // A change is an answer too, and it outranks a read still in flight.
        m_readToken = -1;
        applyEnabled(on);
        setKnown(true);
        if (on)
            prepare();
    } else if (sessionUp(module, name, args)) {
        // The per-account store the setting lives in is opened with the
        // session, so anything asked for before then answered from nothing.
        refresh();
    }
}

// Hung off the answer to the read rather than off opening the chat: the fetch
// goes out every time it is asked for, cache or no cache, so a chat the user
// has turned encryption off for should not pay for it. Once per chat is enough
// - the latch clears when the chat does, and on a reconnect, where the keys
// have to be asked for again anyway.
//
// Fire-and-forget on purpose, and not only because a failed warm-up has nothing
// to report: prepareChat answers its callback with two bare words, which is one
// more than the reply layer can take, so a request with a token wedges rather
// than returning.
void OmemoChat::prepare() {
    if (m_prepared || !m_backend || !available())
        return;
    m_prepared = true;
    m_backend->notify(QStringLiteral("omemo"), QStringLiteral("prepareChat"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jid"), m_jid}});
}

void OmemoChat::applyEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    emit enabledChanged();
}
