#include "MucRoomModel.h"

#include <algorithm>

namespace {
// The four buckets the page groups rows into. `none` and an empty role are the
// same thing here - somebody the room lists without saying what they are.
QString groupFor(const QString &role) {
    if (role == QLatin1String("moderator"))
        return QStringLiteral("moderator");
    if (role == QLatin1String("participant"))
        return QStringLiteral("participant");
    if (role == QLatin1String("visitor"))
        return QStringLiteral("visitor");
    return QStringLiteral("other");
}

int groupRank(const QString &group) {
    if (group == QLatin1String("moderator"))
        return 0;
    if (group == QLatin1String("participant"))
        return 1;
    if (group == QLatin1String("visitor"))
        return 2;
    return 3;
}

// Chat JIDs carry a ?join suffix to mark them as a room's; the muc module keys
// its rooms by the JID underneath, and would find nothing under the suffixed
// form. Same cut AvatarController makes, for the same reason.
QString stripJoin(const QString &jid) {
    if (jid.endsWith(QLatin1String("?join")))
        return jid.left(jid.size() - 5);
    return jid;
}
} // namespace

QString MucRoomModel::Occupant::group() const { return groupFor(role); }

bool MucRoomModel::Occupant::sameAs(const Occupant &other) const {
    return nick == other.nick && realJid == other.realJid && role == other.role
           && affiliation == other.affiliation && show == other.show
           && status == other.status && caps == other.caps;
}

MucRoomModel::MucRoomModel(QObject *parent) : QAbstractListModel(parent) {}

int MucRoomModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MucRoomModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Occupant &o = m_rows.at(index.row());
    switch (role) {
    case NickRole:
        return o.nick;
    case OccupantJidRole:
        return occupantJid(o.nick);
    case RealJidRole:
        return o.realJid;
    case RoleRole:
        return o.role;
    case AffiliationRole:
        return o.affiliation;
    case ShowRole:
        return o.show;
    case StatusRole:
        return o.status;
    case CapsRole:
        return o.caps;
    case SelfRole:
        return !m_myNick.isEmpty() && o.nick == m_myNick;
    case GroupRole:
        return o.group();
    default:
        return {};
    }
}

QHash<int, QByteArray> MucRoomModel::roleNames() const {
    return {
        {NickRole, "nick"},
        {OccupantJidRole, "occupantJid"},
        {RealJidRole, "realJid"},
        {RoleRole, "role"},
        {AffiliationRole, "affiliation"},
        {ShowRole, "show"},
        {StatusRole, "status"},
        {CapsRole, "caps"},
        {SelfRole, "self"},
        {GroupRole, "group"},
    };
}

void MucRoomModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::event, this, &MucRoomModel::handleEvent);
        connect(m_backend, &TackyBackend::result, this, &MucRoomModel::handleResult);
        connect(m_backend, &TackyBackend::error, this, &MucRoomModel::handleError);
        connect(m_backend, &TackyBackend::connected, this, &MucRoomModel::refresh);
    }
    emit backendChanged();
    refresh();
}

void MucRoomModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    refresh();
}

void MucRoomModel::setJid(const QString &jid) {
    if (m_jid == jid)
        return;
    m_jid = jid;
    m_roomJid = stripJoin(jid);
    emit jidChanged();
    refresh();
}

void MucRoomModel::setFilter(const QString &text) {
    if (m_filter == text)
        return;
    m_filter = text;
    emit filterChanged();
    rebuild();
}

QString MucRoomModel::myRole() const {
    for (const Occupant &o : m_occupants)
        if (o.nick == m_myNick)
            return o.role;
    return {};
}

QString MucRoomModel::myAffiliation() const {
    for (const Occupant &o : m_occupants)
        if (o.nick == m_myNick)
            return o.affiliation;
    return {};
}

QVariantMap MucRoomModel::groupCounts() const {
    QVariantMap counts;
    for (const Occupant &o : m_occupants) {
        const QString g = o.group();
        counts.insert(g, counts.value(g).toInt() + 1);
    }
    return counts;
}

void MucRoomModel::refresh() {
    // Whichever of them changed, what is on screen belongs to the room we were
    // showing before.
    clearRoom();

    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty())
        return;

    const QVariantMap args{{QStringLiteral("acc"), m_account},
                           {QStringLiteral("jid"), m_roomJid}};
    m_occupantsToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("occupants"), args);
    m_subjectToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("getSubject"), args);
    m_nickToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("myNick"), args);
    m_joinedToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("isJoined"), args);
}

void MucRoomModel::clearRoom() {
    m_occupants.clear();
    rebuild();
    applySubject({});
    applyMyNick({});
    applyJoined(false);
}

MucRoomModel::Occupant MucRoomModel::fromMap(const QVariantMap &m) {
    Occupant o;
    o.nick = m.value(QStringLiteral("nick")).toString();
    o.realJid = m.value(QStringLiteral("jid")).toString();
    o.role = m.value(QStringLiteral("role")).toString();
    o.affiliation = m.value(QStringLiteral("affiliation")).toString();
    o.show = m.value(QStringLiteral("show")).toString();
    o.status = m.value(QStringLiteral("status")).toString();
    o.caps = m.value(QStringLiteral("caps")).toMap();
    return o;
}

int MucRoomModel::compare(const Occupant &a, const Occupant &b) {
    const int byGroup = groupRank(a.group()) - groupRank(b.group());
    if (byGroup != 0)
        return byGroup;
    // Case-insensitively, so "alice" and "Bob" read as one list rather than as
    // two; exactly when that ties, so the order is total and the merge below
    // never meets two rows it cannot tell apart.
    const int byNick = QString::compare(a.nick, b.nick, Qt::CaseInsensitive);
    return byNick != 0 ? byNick : QString::compare(a.nick, b.nick);
}

QString MucRoomModel::occupantJid(const QString &nick) const {
    if (m_roomJid.isEmpty() || nick.isEmpty())
        return {};
    return m_roomJid + QLatin1Char('/') + nick;
}

bool MucRoomModel::matches(const Occupant &o, const QString &filter) {
    if (filter.isEmpty())
        return true;
    return o.nick.contains(filter, Qt::CaseInsensitive)
           || o.realJid.contains(filter, Qt::CaseInsensitive);
}

// A merge over two lists in the same order: what only the old side has left,
// what only the new side has arrived, and what both hold gets compared field by
// field. Anything coarser would reset the model on every presence, which in a
// busy room throws the scroll position away several times a minute.
void MucRoomModel::rebuild() {
    QList<Occupant> next;
    next.reserve(m_occupants.size());
    for (const Occupant &o : m_occupants)
        if (matches(o, m_filter))
            next.append(o);

    int i = 0; // into m_rows
    int j = 0; // into next
    while (i < m_rows.size() || j < next.size()) {
        int c;
        if (j >= next.size())
            c = -1; // only the old side has rows left, so they have gone
        else if (i >= m_rows.size())
            c = 1; // only the new side, so they have arrived
        else
            c = compare(m_rows.at(i), next.at(j));

        if (c < 0) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        } else if (c > 0) {
            beginInsertRows({}, i, i);
            m_rows.insert(i, next.at(j));
            endInsertRows();
            ++i;
            ++j;
        } else {
            if (!m_rows.at(i).sameAs(next.at(j))) {
                m_rows[i] = next.at(j);
                emit dataChanged(index(i), index(i));
            }
            ++i;
            ++j;
        }
    }
    // `total` and `groupCounts` ride on the same signal and move even when the
    // filter leaves the visible rows alone, so it goes out unconditionally -
    // every path that touches the room ends up here.
    emit countChanged();
}

void MucRoomModel::applyOccupants(const QVariantList &occupants) {
    m_occupants.clear();
    m_occupants.reserve(occupants.size());
    for (const QVariant &v : occupants)
        m_occupants.append(fromMap(v.toMap()));
    std::sort(m_occupants.begin(), m_occupants.end(), less);
    rebuild();
    emit meChanged();
}

void MucRoomModel::applyOccupant(const QVariantMap &map) {
    const Occupant o = fromMap(map);
    if (o.nick.isEmpty())
        return;
    // A role change moves the row between groups, so the old entry is dropped
    // by nick and the new one placed afresh rather than written over in place.
    dropNick(o.nick);
    m_occupants.insert(
        std::lower_bound(m_occupants.begin(), m_occupants.end(), o, less), o);
    rebuild();
    if (o.nick == m_myNick)
        emit meChanged();
}

void MucRoomModel::removeOccupant(const QString &nick) {
    if (!dropNick(nick))
        return;
    rebuild();
    if (nick == m_myNick)
        emit meChanged();
}

bool MucRoomModel::dropNick(const QString &nick) {
    const auto it = std::find_if(
        m_occupants.begin(), m_occupants.end(),
        [&nick](const Occupant &o) { return o.nick == nick; });
    if (it == m_occupants.end())
        return false;
    m_occupants.erase(it);
    return true;
}

void MucRoomModel::applySubject(const QString &text) {
    if (m_subject == text)
        return;
    m_subject = text;
    emit subjectChanged();
}

void MucRoomModel::applyMyNick(const QString &nick) {
    if (m_myNick == nick)
        return;
    m_myNick = nick;
    emit meChanged();
    // Every row's `self` is measured against it.
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(m_rows.size() - 1), {SelfRole});
}

void MucRoomModel::applyJoined(bool joined) {
    if (m_joined == joined)
        return;
    m_joined = joined;
    emit joinedChanged();
}

void MucRoomModel::handleEvent(const QString &module, const QString &name,
                               const QVariant &args) {
    if (module != QLatin1String("muc") || m_account.isEmpty() || m_roomJid.isEmpty())
        return;
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account)
        return;
    if (a.value(QStringLiteral("jid")).toString() != m_roomJid)
        return;

    if (name == QLatin1String("Presence")) {
        applyOccupant(a.value(QStringLiteral("occupant")).toMap());
    } else if (name == QLatin1String("Unavailable")) {
        removeOccupant(a.value(QStringLiteral("nick")).toString());
    } else if (name == QLatin1String("NickChanged")) {
        // The new nick arrives as its own presence; this only retires the old
        // row, and takes our own nick with it when the change was ours.
        if (a.value(QStringLiteral("self")).toBool())
            applyMyNick(a.value(QStringLiteral("newNick")).toString());
        removeOccupant(a.value(QStringLiteral("oldNick")).toString());
    } else if (name == QLatin1String("Joined")) {
        // tacky records the join before it says so, so this is the first moment
        // it has a room to answer for - ask it rather than piece the state
        // together from the event.
        refresh();
    } else if (name == QLatin1String("Left") || name == QLatin1String("Destroyed")) {
        clearRoom();
    } else if (name == QLatin1String("Subject")) {
        applySubject(a.value(QStringLiteral("subject")).toString());
    }
}

void MucRoomModel::handleResult(int token, const QVariant &data) {
    // 0 is what the fields below hold with nothing in flight, and no real token
    // is ever 0, so this is what keeps an idle field from matching.
    if (token == 0)
        return;
    if (token == m_occupantsToken) {
        m_occupantsToken = 0;
        applyOccupants(data.toList());
    } else if (token == m_subjectToken) {
        m_subjectToken = 0;
        applySubject(data.toString());
    } else if (token == m_nickToken) {
        m_nickToken = 0;
        applyMyNick(data.toString());
    } else if (token == m_joinedToken) {
        m_joinedToken = 0;
        applyJoined(data.toBool());
    } else {
        // A moderation action that went through. The room reports what it did
        // by presence, so there is nothing here to apply.
        m_actions.remove(token);
    }
}

void MucRoomModel::handleError(int token, const QString &message) {
    const QString action = m_actions.take(token);
    if (!action.isEmpty())
        emit actionFailed(action, message);
}

void MucRoomModel::sendAction(const QString &label, const QString &method,
                              QVariantMap args) {
    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty())
        return;
    args.insert(QStringLiteral("acc"), m_account);
    args.insert(QStringLiteral("jid"), m_roomJid);
    m_actions.insert(m_backend->request(QStringLiteral("muc"), method, args), label);
}

void MucRoomModel::notifyRoom(const QString &method, QVariantMap args) {
    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty())
        return;
    args.insert(QStringLiteral("acc"), m_account);
    args.insert(QStringLiteral("jid"), m_roomJid);
    m_backend->notify(QStringLiteral("muc"), method, args);
}

void MucRoomModel::kick(const QString &nick, const QString &reason) {
    if (nick.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("nick"), nick}};
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(QStringLiteral("Kick"), QStringLiteral("kick"), args);
}

void MucRoomModel::setRole(const QString &nick, const QString &role,
                           const QString &reason) {
    if (nick.isEmpty() || role.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("nick"), nick}, {QStringLiteral("role"), role}};
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(QStringLiteral("Role change"), QStringLiteral("role"), args);
}

void MucRoomModel::setAffiliation(const QString &targetJid,
                                  const QString &affiliation,
                                  const QString &reason) {
    // Affiliations are written against a real JID, which a semi-anonymous room
    // withholds; without one there is nothing to send.
    if (targetJid.isEmpty() || affiliation.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("target"), targetJid},
                     {QStringLiteral("affiliation"), affiliation}};
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(QStringLiteral("Affiliation change"), QStringLiteral("affiliation"),
               args);
}

void MucRoomModel::destroyRoom(const QString &reason) {
    QVariantMap args;
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(QStringLiteral("Destroy room"), QStringLiteral("destroyRoom"), args);
}

void MucRoomModel::invite(const QString &jid, const QString &reason) {
    if (jid.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("to"), jid}};
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    notifyRoom(QStringLiteral("invite"), args);
}

void MucRoomModel::setSubject(const QString &text) {
    notifyRoom(QStringLiteral("subject"), {{QStringLiteral("body"), text}});
}

void MucRoomModel::requestVoice() {
    notifyRoom(QStringLiteral("requestVoice"), {});
}

void MucRoomModel::changeNick(const QString &nick) {
    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty() || nick.isEmpty())
        return;
    m_backend->notify(QStringLiteral("bookmarks"), QStringLiteral("nick"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jid"), m_roomJid},
                                  {QStringLiteral("nick"), nick}});
}
