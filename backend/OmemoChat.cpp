#include "OmemoChat.h"

OmemoChat::OmemoChat(QObject *parent) : QObject(parent) {}

void OmemoChat::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        connect(m_backend, &TackyBackend::event, this, &OmemoChat::handleEvent);
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
// out. Back to the default until this chat's own answer lands.
void OmemoChat::refresh() {
    applyEnabled(true);
    m_prepared = false;
    if (!m_backend || !available())
        return;
    // No getter exists for this: `pull` re-emits <Enabled> with the stored
    // value, which the handler below picks up like any other change. The event
    // name goes bare - tacky's json layer puts the brackets back.
    m_backend->notify(QStringLiteral("omemo"), QStringLiteral("pull"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jid"), m_jid},
                                  {QStringLiteral("event"), QStringLiteral("Enabled")}});
}

void OmemoChat::setEnabled(bool on) {
    if (!available() || m_enabled == on)
        return;
    // Optimistic, as with blind trust: the padlock follows the click and
    // <Enabled> confirms it.
    applyEnabled(on);
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
        applyEnabled(on);
        if (on)
            prepare();
    } else if (module == QLatin1String("conn") && name == QLatin1String("Ready")) {
        // The per-account store the setting lives in is opened on <Ready>, so
        // anything asked for before then answered from nothing. Ask again.
        refresh();
    }
}

// Hung off the answer to the pull rather than off opening the chat: the fetch
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
