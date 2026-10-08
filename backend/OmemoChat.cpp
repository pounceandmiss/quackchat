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
    m_setToken = -1;
    m_prepared = false;
    applyRoomStatus({});
    if (!m_backend || m_account.isEmpty() || m_jid.isEmpty())
        return;
    m_readToken = m_backend->request(
        QStringLiteral("omemo"),
        m_groupchat ? QStringLiteral("roomStatus") : QStringLiteral("isEnabled"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("jid"), m_jid}});
}

void OmemoChat::handleResult(int token, const QVariant &data) {
    if (token == m_setToken) {
        m_setToken = -1;
        return;
    }
    if (token != m_readToken)
        return;
    m_readToken = -1;
    if (m_groupchat) {
        const QVariantMap status = data.toMap();
        applyRoomStatus(status);
        applyEnabled(status.value(QStringLiteral("enabled")).toBool());
    } else {
        applyEnabled(data.toBool());
    }
    setKnown(true);
    if (m_enabled)
        prepare();
}

// A failed read is left unknown rather than guessed at: the next connected
// edge asks again, and until then there is nothing to draw. A refused switch
// (the room stopped qualifying between drawing it and the click) is put back
// by reading the room again.
void OmemoChat::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token == m_setToken) {
        m_setToken = -1;
        refresh();
        return;
    }
    if (token != m_readToken)
        return;
    m_readToken = -1;
}

void OmemoChat::setKnown(bool v) {
    if (m_known == v)
        return;
    const bool was = available();
    m_known = v;
    emit knownChanged();
    availableFrom(was);
}

void OmemoChat::availableFrom(bool was) {
    if (was != available())
        emit availableChanged();
}

void OmemoChat::applyRoomStatus(const QVariantMap &status) {
    const bool was = available();
    const bool eligible = status.value(QStringLiteral("eligible")).toBool();
    const QStringList reasons = status.value(QStringLiteral("reasons")).toStringList();
    const QString memberList = status.value(QStringLiteral("member_list")).toString();
    const int attention = status.value(QStringLiteral("attention")).toInt();
    const bool offered = status.value(QStringLiteral("offered")).toBool();
    if (eligible != m_eligible || reasons != m_reasons || memberList != m_memberList
        || attention != m_attention || offered != m_offered) {
        m_eligible = eligible;
        m_reasons = reasons;
        m_memberList = memberList;
        m_attention = attention;
        m_offered = offered;
        emit roomStatusChanged();
    }
    setUnreachable(status.value(QStringLiteral("unreachable")).toList());
    availableFrom(was);
}

void OmemoChat::setUnreachable(const QVariantList &members) {
    if (m_unreachable == members)
        return;
    m_unreachable = members;
    emit unreachableChanged();
}

void OmemoChat::dismissUnreachable() {
    setUnreachable({});
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
    const QVariantMap args{{QStringLiteral("acc"), m_account},
                           {QStringLiteral("jid"), m_jid},
                           {QStringLiteral("value"), on ? 1 : 0}};
    if (m_groupchat)
        m_setToken = m_backend->request(QStringLiteral("omemo"),
                                        QStringLiteral("setEnabled"), args);
    else
        m_backend->notify(QStringLiteral("omemo"), QStringLiteral("setEnabled"),
                          args);
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
    } else if (module == QLatin1String("omemo") && name == QLatin1String("RoomStatus")) {
        if (!m_groupchat || a.value(QStringLiteral("jid")).toString() != m_jid)
            return;
        // The whole answer, the switch included, as the read's is.
        const QVariantMap status = a.value(QStringLiteral("status")).toMap();
        m_readToken = -1;
        applyRoomStatus(status);
        applyEnabled(status.value(QStringLiteral("enabled")).toBool());
        setKnown(true);
        if (m_enabled)
            prepare();
    } else if (module == QLatin1String("omemo")
               && name == QLatin1String("MembersUnreachable")) {
        // Its own event as well as part of <RoomStatus>, which says only what
        // changed: a second send stopped by the same members is news here and
        // nowhere else.
        if (!m_groupchat || a.value(QStringLiteral("jid")).toString() != m_jid)
            return;
        setUnreachable(a.value(QStringLiteral("members")).toList());
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
    const bool was = available();
    m_enabled = on;
    emit enabledChanged();
    availableFrom(was);
}
