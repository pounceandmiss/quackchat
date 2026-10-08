#include "MucRoomModel.h"

#include "BackendBinding.h"

#include <QSet>

MucRoomModel::MucRoomModel(QObject *parent) : QAbstractListModel(parent) {}

int MucRoomModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MucRoomModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Person &p = m_rows.at(index.row());
    switch (role) {
    case KeyRole:
        return p.key;
    case NickRole:
        return p.nick;
    case OccupantJidRole:
        return p.occupantJid;
    case RealJidRole:
        return p.realJid;
    case RoleRole:
        return p.role;
    case AffiliationRole:
        return p.affiliation;
    case ShowRole:
        return p.show;
    case StatusRole:
        return p.status;
    case CapsRole:
        return p.caps;
    case SelfRole:
        return p.self;
    case GroupRole:
        return p.group;
    case PresentRole:
        return p.present;
    case KeysRole:
        return p.keys;
    default:
        return {};
    }
}

QHash<int, QByteArray> MucRoomModel::roleNames() const {
    return {
        {KeyRole, "key"},
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
        {PresentRole, "present"},
        {KeysRole, "keys"},
    };
}

void MucRoomModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &MucRoomModel::refresh);
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
    // The muc module keys its rooms by the JID under the suffix, and would
    // find nothing under the suffixed form.
    m_roomJid = jidWithoutJoin(jid);
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

void MucRoomModel::setActive(bool on) {
    if (m_active == on)
        return;
    m_active = on;
    emit activeChanged();
    if (m_active && m_stale)
        readPeople();
}

MucRoomModel::Person MucRoomModel::mine() const {
    for (const Person &p : m_people)
        if (p.self)
            return p;
    return {};
}

void MucRoomModel::refresh() {
    // Whichever of them changed, what is on screen belongs to the room we were
    // showing before.
    clearRoom();
    m_stale = true;

    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty())
        return;

    const QVariantMap args{{QStringLiteral("acc"), m_account},
                           {QStringLiteral("jid"), m_roomJid}};
    m_subjectToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("getSubject"), args);
    m_joinedToken =
        m_backend->request(QStringLiteral("muc"), QStringLiteral("isJoined"), args);
    if (m_active)
        readPeople();
}

// Asked again rather than patched from the event: the event only says that
// something changed, and the whole answer is what the page shows.
void MucRoomModel::readPeople() {
    m_stale = true;
    if (!m_backend || m_account.isEmpty() || m_roomJid.isEmpty())
        return;
    m_stale = false;
    m_peopleToken = m_backend->request(
        QStringLiteral("muc"), QStringLiteral("people"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("jid"), m_roomJid}});
}

void MucRoomModel::clearRoom() {
    applyPeople({});
    applySubject({});
    applyJoined(false);
}

MucRoomModel::Person MucRoomModel::fromMap(const QVariantMap &m) {
    Person p;
    p.key = m.value(QStringLiteral("key")).toString();
    p.nick = m.value(QStringLiteral("nick")).toString();
    p.occupantJid = m.value(QStringLiteral("occupant")).toString();
    p.realJid = m.value(QStringLiteral("jid")).toString();
    p.role = m.value(QStringLiteral("role")).toString();
    p.affiliation = m.value(QStringLiteral("affiliation")).toString();
    p.show = m.value(QStringLiteral("show")).toString();
    p.status = m.value(QStringLiteral("status")).toString();
    p.caps = m.value(QStringLiteral("caps")).toMap();
    p.self = m.value(QStringLiteral("self")).toBool();
    p.group = m.value(QStringLiteral("group")).toString();
    p.present = m.value(QStringLiteral("present")).toBool();
    p.keys = m.value(QStringLiteral("keys")).toMap();
    return p;
}

bool MucRoomModel::matches(const Person &p, const QString &filter) {
    if (filter.isEmpty())
        return true;
    return p.nick.contains(filter, Qt::CaseInsensitive)
           || p.realJid.contains(filter, Qt::CaseInsensitive);
}

// The rows walked onto the new list one place at a time, by key: a row that
// stayed is updated in place, one that moved is moved, and only what came or
// went is inserted or removed. Anything coarser would reset the model on every
// presence, which in a busy room throws the scroll position away several times
// a minute.
void MucRoomModel::rebuild() {
    QList<Person> next;
    next.reserve(m_people.size());
    for (const Person &p : m_people)
        if (matches(p, m_filter))
            next.append(p);

    QSet<QString> wanted;
    for (const Person &p : next)
        wanted.insert(p.key);
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (wanted.contains(m_rows.at(i).key))
            continue;
        beginRemoveRows({}, i, i);
        m_rows.removeAt(i);
        endRemoveRows();
    }

    for (int j = 0; j < next.size(); ++j) {
        if (j < m_rows.size() && m_rows.at(j).key == next.at(j).key) {
            if (!m_rows.at(j).sameAs(next.at(j))) {
                m_rows[j] = next.at(j);
                emit dataChanged(index(j), index(j));
            }
            continue;
        }
        int k = j + 1;
        while (k < m_rows.size() && m_rows.at(k).key != next.at(j).key)
            ++k;
        if (k < m_rows.size()) {
            beginMoveRows({}, k, k, {}, j);
            m_rows.move(k, j);
            endMoveRows();
            if (!m_rows.at(j).sameAs(next.at(j))) {
                m_rows[j] = next.at(j);
                emit dataChanged(index(j), index(j));
            }
        } else {
            beginInsertRows({}, j, j);
            m_rows.insert(j, next.at(j));
            endInsertRows();
        }
    }
    // `total` and `groupCounts` ride on the same signal and move even when the
    // filter leaves the visible rows alone, so it goes out unconditionally -
    // every path that touches the room ends up here.
    emit countChanged();
}

void MucRoomModel::applyPeople(const QVariantMap &answer) {
    const Person before = mine();
    const QVariantMap me = answer.value(QStringLiteral("me")).toMap();
    m_people.clear();
    for (const QVariant &v : answer.value(QStringLiteral("people")).toList())
        m_people.append(fromMap(v.toMap()));
    m_groups = answer.value(QStringLiteral("groups")).toMap();
    m_memberList = answer.value(QStringLiteral("list")).toString();
    rebuild();
    if (!mine().sameAs(before) || me != m_me) {
        m_me = me;
        emit meChanged();
    }
}

void MucRoomModel::applySubject(const QString &text) {
    if (m_subject == text)
        return;
    m_subject = text;
    emit subjectChanged();
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

    if (name == QLatin1String("PeopleChanged")) {
        if (m_active)
            readPeople();
        else
            m_stale = true;
    } else if (name == QLatin1String("Joined")) {
        // tacky records the join before it says so, so this is the first moment
        // it has a room to answer for.
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
    if (token == m_peopleToken) {
        m_peopleToken = 0;
        applyPeople(data.toMap());
    } else if (token == m_subjectToken) {
        m_subjectToken = 0;
        applySubject(data.toString());
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
    sendAction(tr("Kick"), QStringLiteral("kick"), args);
}

void MucRoomModel::setRole(const QString &nick, const QString &role,
                           const QString &reason) {
    if (nick.isEmpty() || role.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("nick"), nick}, {QStringLiteral("role"), role}};
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(tr("Role change"), QStringLiteral("role"), args);
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
    sendAction(tr("Affiliation change"), QStringLiteral("affiliation"),
               args);
}

void MucRoomModel::destroyRoom(const QString &reason) {
    QVariantMap args;
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendAction(tr("Destroy room"), QStringLiteral("destroyRoom"), args);
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
