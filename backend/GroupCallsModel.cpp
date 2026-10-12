#include "GroupCallsModel.h"

#include <QSet>

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
    return c;
}

void GroupCallsModel::refreshFor(const QString &acc) {
    if (!m_backend || acc.isEmpty())
        return;
    for (auto it = m_listTokens.begin(); it != m_listTokens.end();)
        it = it->acc == acc ? m_listTokens.erase(it) : std::next(it);
    const int token = m_backend->request(
        QStringLiteral("groupcall"), QStringLiteral("get"),
        QVariantMap{{QStringLiteral("acc"), acc}});
    m_listTokens.insert(token, {acc});
}

// The chat a state names; else the chat whose call is in that room; else the
// room itself. Made if need be, so a page opened later finds its banner state.
GroupCall *GroupCallsModel::callOf(const QString &acc, const QVariantMap &state) {
    const QString room = state.value(QStringLiteral("jid")).toString();
    const QString chat = state.value(QStringLiteral("chat")).toString();
    if (!chat.isEmpty())
        return callFor(acc, chat);
    if (GroupCall *c = findByCall(acc, room))
        return c;
    return callFor(acc, room);
}

void GroupCallsModel::handleEvent(const QString &module, const QString &name,
                                  const QVariant &args) {
    const QVariantMap a = args.toMap();
    const QString acc = a.value(QStringLiteral("acc")).toString();

    if (sessionUp(module, name, args)) {
        refreshFor(acc);
        return;
    }
    if (module != QLatin1String("groupcall"))
        return;

    if (name == QLatin1String("State")) {
        if (GroupCall *c = callOf(acc, a))
            c->applyState(a);
    } else if (name == QLatin1String("Invited")) {
        const QString chat = a.value(QStringLiteral("chat")).toString();
        // A row for the chat, so the answer finds its call.
        callFor(acc, chat);
        emit invited(acc, chat, a.value(QStringLiteral("timestamp")).toLongLong(),
                     a.value(QStringLiteral("jid")).toString(),
                     a.value(QStringLiteral("from")).toString(),
                     a.value(QStringLiteral("video")).toBool());
    } else if (name == QLatin1String("StartFailed")) {
        if (GroupCall *c = callFor(acc, a.value(QStringLiteral("chat")).toString()))
            c->startFailed(a.value(QStringLiteral("reason")).toString());
    }
}

void GroupCallsModel::handleResult(int token, const QVariant &data) {
    const auto it = m_listTokens.constFind(token);
    if (it == m_listTokens.cend())
        return;
    const QString acc = it->acc;
    m_listTokens.erase(it);
    QSet<GroupCall *> named;
    for (const QVariant &v : data.toList()) {
        const QVariantMap st = v.toMap();
        if (GroupCall *c = callOf(acc, st)) {
            c->applyState(st);
            named.insert(c);
        }
    }
    // Left out: ended, or emptied, while no UI was here.
    const QList<GroupCall *> calls = m_calls;
    for (GroupCall *c : calls)
        if (c->account() == acc && !named.contains(c))
            c->forget();
}

void GroupCallsModel::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    m_listTokens.remove(token);
}
