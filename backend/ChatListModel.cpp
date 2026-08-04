#include "ChatListModel.h"

#include "TackyBackend.h"

#include <algorithm>

ChatListModel::ChatListModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

// Role order: kKeys[i] is Qt::UserRole+1+i. The QML role name is also the tacky
// storage key, so data() and roleNames() both come off this list - keep it in
// step with the Role enum. RawRole is separate (whole map, no key).
static const QList<QByteArray> kKeys = {
    "jid", "name", "source", "groupchat",
    "autojoin", "last_activity", "subscription", "room_state",
};

QVariant ChatListModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_items.size())
        return {};
    const QVariantMap &e = m_items.at(index.row());
    if (role == RawRole)
        return e;
    const int i = role - (Qt::UserRole + 1);
    if (i < 0 || i >= kKeys.size())
        return {};
    return e.value(QString::fromLatin1(kKeys.at(i)));
}

QHash<int, QByteArray> ChatListModel::roleNames() const {
    QHash<int, QByteArray> r;
    for (int i = 0; i < kKeys.size(); ++i)
        r.insert(Qt::UserRole + 1 + i, kKeys.at(i));
    r.insert(RawRole, "raw");
    return r;
}

void ChatListModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    refresh();
}

void ChatListModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::event, this, &ChatListModel::handleEvent);
        connect(m_backend, &TackyBackend::result, this, &ChatListModel::handleResult);
    }
    emit backendChanged();
    refresh();
}

void ChatListModel::refresh() {
    if (!m_backend || m_account.isEmpty())
        return;
    m_getToken = m_backend->request(QStringLiteral("chatlist"),
                                    QStringLiteral("get"),
                                    QVariantMap{{QStringLiteral("acc"), m_account}});
}

void ChatListModel::handleEvent(const QString &module, const QString &name,
                                const QVariant &args) {
    if (module != QLatin1String("chatlist"))
        return;
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account)
        return;

    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    if (name == QLatin1String("Item"))
        applyItem(a.value(QStringLiteral("item")).toMap());
    else if (name == QLatin1String("Remove"))
        applyRemove(a.value(QStringLiteral("jid")).toString());
    else if (name == QLatin1String("Changed"))
        refresh();
}

void ChatListModel::handleResult(int token, const QVariant &data) {
    if (token == m_getToken)
        applyList(data.toList());
}

bool ChatListModel::lessThan(const QVariantMap &a, const QVariantMap &b) {
    const qlonglong la = a.value(QStringLiteral("last_activity")).toLongLong();
    const qlonglong lb = b.value(QStringLiteral("last_activity")).toLongLong();
    if (la != lb)
        return la > lb; // newest activity first
    const QString na = a.value(QStringLiteral("name")).toString();
    const QString nb = b.value(QStringLiteral("name")).toString();
    const int c = na.compare(nb, Qt::CaseInsensitive);
    if (c != 0)
        return c < 0;
    return a.value(QStringLiteral("jid")).toString() <
           b.value(QStringLiteral("jid")).toString();
}

int ChatListModel::indexOfJid(const QString &jid) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).value(QStringLiteral("jid")).toString() == jid)
            return i;
    return -1;
}

int ChatListModel::insertPos(const QVariantMap &entry) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (lessThan(entry, m_items.at(i)))
            return i;
    return m_items.size();
}

void ChatListModel::applyList(const QVariantList &entries) {
    beginResetModel();
    m_items.clear();
    m_items.reserve(entries.size());
    for (const QVariant &v : entries)
        m_items.append(v.toMap());
    std::sort(m_items.begin(), m_items.end(), &ChatListModel::lessThan);
    endResetModel();
}

void ChatListModel::applyItem(const QVariantMap &entry) {
    const QString jid = entry.value(QStringLiteral("jid")).toString();
    if (jid.isEmpty())
        return;

    const int old = indexOfJid(jid);
    if (old < 0) {
        const int pos = insertPos(entry);
        beginInsertRows({}, pos, pos);
        m_items.insert(pos, entry);
        endInsertRows();
        return;
    }

    // Replace the entry, then reposition it if the sort key moved. target is
    // where it belongs among the *other* rows.
    m_items[old] = entry;
    int target = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i == old)
            continue;
        if (lessThan(entry, m_items.at(i)))
            break;
        ++target;
    }
    if (target == old) {
        const QModelIndex idx = index(old);
        emit dataChanged(idx, idx);
        return;
    }
    // Remove + insert rather than beginMoveRows; same result, less fiddly.
    beginRemoveRows({}, old, old);
    m_items.removeAt(old);
    endRemoveRows();
    beginInsertRows({}, target, target);
    m_items.insert(target, entry);
    endInsertRows();
}

void ChatListModel::applyRemove(const QString &jid) {
    const int i = indexOfJid(jid);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_items.removeAt(i);
    endRemoveRows();
}
