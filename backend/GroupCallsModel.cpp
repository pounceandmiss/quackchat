#include "GroupCallsModel.h"

#include "BackendBinding.h"
#include "TackyBackend.h"

GroupCallsModel::GroupCallsModel(QObject *parent) : QAbstractListModel(parent) {}

int GroupCallsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_calls.size();
}

QVariant GroupCallsModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_calls.size())
        return {};
    GroupCall *c = m_calls.at(index.row());
    switch (role) {
    case CallRole:    return QVariant::fromValue<QObject *>(c);
    case AccountRole: return c->account();
    case JidRole:     return c->jid();
    default:          return {};
    }
}

QHash<int, QByteArray> GroupCallsModel::roleNames() const {
    return {{CallRole, "call"}, {AccountRole, "account"}, {JidRole, "jid"}};
}

void GroupCallsModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    // As CallsModel: the connected edge names no account, and every read here
    // takes one, so the per-account conn events are the re-seed.
    if (m_backend)
        bindBackendWithoutReseed(this, m_backend);
}

// The chat list keys a room by `room@muc?join`; the groupcall module answers
// to the bare room, in the lower case it normalises to.
QString GroupCallsModel::roomJid(const QString &jid) {
    QString room = jidWithoutJoin(jid.trimmed());
    const int slash = room.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        room.truncate(slash);
    return room.toLower();
}

GroupCall *GroupCallsModel::find(const QString &acc, const QString &jid) const {
    const QString room = roomJid(jid);
    for (GroupCall *c : m_calls)
        if (c->account() == acc && c->jid() == room)
            return c;
    return nullptr;
}

GroupCall *GroupCallsModel::findByCall(const QString &acc, const QString &room) const {
    const QString r = roomJid(room);
    for (GroupCall *c : m_calls)
        if (c->account() == acc && !c->callJid().isEmpty() && c->callJid() == r)
            return c;
    return nullptr;
}

GroupCall *GroupCallsModel::callFor(const QString &acc, const QString &jid) {
    const QString room = roomJid(jid);
    if (acc.isEmpty() || room.isEmpty())
        return nullptr;
    if (GroupCall *c = find(acc, room))
        return c;
    auto *c = new GroupCall(m_backend, acc, room, this);
    const int pos = m_calls.size();
    beginInsertRows({}, pos, pos);
    m_calls.append(c);
    endInsertRows();
    emit countChanged();
    c->refresh();
    return c;
}

void GroupCallsModel::refreshFor(const QString &acc) {
    if (!m_backend || acc.isEmpty())
        return;
    for (auto it = m_listTokens.begin(); it != m_listTokens.end();)
        it = it->acc == acc ? m_listTokens.erase(it) : std::next(it);
    const int token = m_backend->request(
        QStringLiteral("groupcall"), QStringLiteral("list"),
        QVariantMap{{QStringLiteral("acc"), acc}});
    m_listTokens.insert(token, {acc});
    for (GroupCall *c : std::as_const(m_calls))
        if (c->account() == acc)
            c->refresh();
}

void GroupCallsModel::handleEvent(const QString &module, const QString &name,
                                  const QVariant &args) {
    const QVariantMap a = args.toMap();
    const QString acc = a.value(QStringLiteral("acc")).toString();

    if (sessionUp(module, name, args)) {
        refreshFor(acc);
        return;
    }
    if (module == QLatin1String("calls")) {
        if (a.value(QStringLiteral("sid")).toString().isEmpty())
            return;
        for (GroupCall *c : std::as_const(m_calls))
            if (c->account() == acc && c->handleLegEvent(name, a))
                return;
        return;
    }
    if (module == QLatin1String("muc")) {
        // A call's participants and a chat's banner are both read off a
        // room's occupants, so whichever call reads this room reads it again.
        const QString room = roomJid(a.value(QStringLiteral("jid")).toString());
        for (GroupCall *c : std::as_const(m_calls))
            if (c->account() == acc && c->statusRoom() == room)
                c->handleRoomEvent(name, a);
        return;
    }
    if (module != QLatin1String("groupcall"))
        return;

    const QString room = a.value(QStringLiteral("jid")).toString();
    const QString chat = a.value(QStringLiteral("chat")).toString();
    if (name == QLatin1String("Invited")) {
        // A row for the chat, so the answer finds its call.
        callFor(acc, chat);
        emit invited(acc, chat, a.value(QStringLiteral("timestamp")).toLongLong(),
                     room, a.value(QStringLiteral("from")).toString(),
                     a.value(QStringLiteral("video")).toBool());
        return;
    }
    // The chat the event names; else the chat whose call is in that room;
    // else the room itself, an in-room call's chat. Either way a chat the
    // backend speaks of gets a row, so a page opened on it later finds its
    // banner state already here.
    GroupCall *c = !chat.isEmpty() ? callFor(acc, chat) : findByCall(acc, room);
    if (!c)
        c = callFor(acc, room);
    if (c)
        c->handleEvent(name, a);
}

void GroupCallsModel::handleResult(int token, const QVariant &data) {
    if (const auto it = m_listTokens.constFind(token); it != m_listTokens.cend()) {
        const QString acc = it->acc;
        m_listTokens.erase(it);
        const QVariantList rows = data.toList();
        for (const QVariant &v : rows) {
            const QVariantMap r = v.toMap();
            const QString room = r.value(QStringLiteral("jid")).toString();
            const QString chat = r.value(QStringLiteral("chat")).toString();
            if (GroupCall *c = callFor(acc, chat.isEmpty() ? room : chat)) {
                c->setCallJid(room);
                c->applyListed(r.value(QStringLiteral("video")).toBool(),
                               r.value(QStringLiteral("preview")).toMap(),
                               r.value(QStringLiteral("sessions")).toMap());
            }
        }
        return;
    }
}

void GroupCallsModel::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    m_listTokens.remove(token);
}
