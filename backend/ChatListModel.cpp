#include "ChatListModel.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

#include <algorithm>

// In Role order, which is what lines the keys up with the roles.
ChatListModel::ChatListModel(QObject *parent)
    : MapListModel({"jid", "name", "source", "groupchat", "autojoin",
                    "last_activity", "subscription", "room_state", "room_reason",
                    "unread", "unread_mentions", "invited", "preview",
                    "is_self", "pinned", "order"},
                   parent) {}

// One chat's entry, for the views that hold a JID with no row of this model to
// bind to - a search hit naming its own chat, say. Empty when the list has
// never heard of it, which the caller reads as an unnamed 1:1.
QVariantMap ChatListModel::entryFor(const QString &jid) const {
    const int i = indexOfJid(jid);
    return i < 0 ? QVariantMap() : m_items.at(i);
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
    if (m_backend)
        bindBackend(this, m_backend, &ChatListModel::refresh);
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

void ChatListModel::reload() {
    if (!m_backend || m_account.isEmpty())
        return;
    const QVariantMap acc{{QStringLiteral("acc"), m_account}};
    m_backend->notify(QStringLiteral("roster"), QStringLiteral("request"), acc);
    m_backend->notify(QStringLiteral("bookmarks"), QStringLiteral("request"), acc);
    refresh();
}

// The edits below go out as notifies, not requests: the roster and bookmark
// writers are plain tacky methods rather than tackymethods, so they never call
// back a token - a request would leave one in flight for good.
void ChatListModel::sendEdit(const QString &module, const QString &method,
                             QVariantMap args) {
    if (!m_backend || m_account.isEmpty()
        || args.value(QStringLiteral("jid")).toString().isEmpty())
        return;
    args.insert(QStringLiteral("acc"), m_account);
    m_backend->notify(module, method, args);
}

void ChatListModel::addContact(const QString &jid, const QString &name) {
    QVariantMap args{{QStringLiteral("jid"), jid}};
    // Sent bare rather than as an empty -name, which would clear a name the
    // roster already holds for a contact being re-added.
    if (!name.isEmpty())
        args.insert(QStringLiteral("name"), name);
    // `add`, not `item`: it also asks for the subscription, without which the
    // contact never shows a presence.
    sendEdit(QStringLiteral("roster"), QStringLiteral("add"), args);
}

void ChatListModel::renameContact(const QString &jid, const QString &name) {
    sendEdit(QStringLiteral("roster"), QStringLiteral("item"),
             {{QStringLiteral("jid"), jid}, {QStringLiteral("name"), name}});
}

void ChatListModel::removeContact(const QString &jid) {
    sendEdit(QStringLiteral("roster"), QStringLiteral("remove"),
             {{QStringLiteral("jid"), jid}});
}

void ChatListModel::pin(const QString &jid, bool pinned) {
    sendEdit(QStringLiteral("chatlist"), QStringLiteral("pin"),
             {{QStringLiteral("jid"), jid}, {QStringLiteral("pinned"), pinned}});
}

void ChatListModel::setPinned(const QStringList &jids) {
    if (!m_backend || m_account.isEmpty())
        return;
    m_backend->notify(QStringLiteral("chatlist"), QStringLiteral("setPinned"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jids"), jids}});
}

void ChatListModel::joinRoom(const QString &jid, const QString &nick,
                             const QString &password) {
    QVariantMap args{{QStringLiteral("jid"), jid},
                     {QStringLiteral("autojoin"), 1}};
    // Same reasoning as the contact name: an empty one would overwrite the
    // bookmark's stored nick, and tacky's default fills the gap anyway.
    if (!nick.isEmpty())
        args.insert(QStringLiteral("nick"), nick);
    if (!password.isEmpty())
        args.insert(QStringLiteral("password"), password);
    sendEdit(QStringLiteral("bookmarks"), QStringLiteral("item"), args);
}

void ChatListModel::leaveRoom(const QString &jid) {
    sendEdit(QStringLiteral("bookmarks"), QStringLiteral("leave"),
             {{QStringLiteral("jid"), jid}});
}

void ChatListModel::forceJoinRoom(const QString &jid) {
    sendEdit(QStringLiteral("bookmarks"), QStringLiteral("forceJoin"),
             {{QStringLiteral("jid"), jid}});
}

void ChatListModel::renameBookmark(const QString &jid, const QString &name) {
    sendEdit(QStringLiteral("bookmarks"), QStringLiteral("item"),
             {{QStringLiteral("jid"), jid}, {QStringLiteral("name"), name}});
}

void ChatListModel::removeBookmark(const QString &jid) {
    sendEdit(QStringLiteral("bookmarks"), QStringLiteral("remove"),
             {{QStringLiteral("jid"), jid}});
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
    if (token != m_getToken)
        return;
    setLoadError({});
    applyList(data.toList());
}

void ChatListModel::handleError(int token, const QString &message) {
    if (token == m_getToken)
        setLoadError(message);
}

void ChatListModel::setLoadError(const QString &message) {
    if (m_loadError == message)
        return;
    m_loadError = message;
    emit loadErrorChanged();
}

// `order` largest first, the JID breaking ties.
bool ChatListModel::lessThan(const QVariantMap &a, const QVariantMap &b) {
    const qlonglong oa = a.value(QStringLiteral("order")).toLongLong();
    const qlonglong ob = b.value(QStringLiteral("order")).toLongLong();
    if (oa != ob)
        return oa > ob;
    return a.value(QStringLiteral("jid")).toString() <
           b.value(QStringLiteral("jid")).toString();
}

// A caption-less share: "Photo", the file's name, or how many there are.
static QString attachmentPreview(const QVariantList &atts) {
    if (atts.isEmpty())
        return {};
    if (atts.size() > 1)
        return ChatListModel::tr("%n attachment(s)", nullptr, int(atts.size()));
    const QVariantMap a = atts.first().toMap();
    if (a.value(QStringLiteral("type")).toString() == QLatin1String("image"))
        return ChatListModel::tr("Photo");
    const QString name = a.value(QStringLiteral("name")).toString();
    return name.isEmpty() ? ChatListModel::tr("File") : name;
}

// One line for the row under the chat's name, worded the way tacky's own list
// words it: who said the newest message, then what. Your own are "You:", a
// room-mate's carry their nick, and a 1:1 peer's carry nothing, since the row
// is already named after them. Notes to yourself are all yours, so they carry
// nothing either. Invites and calls ship a fallback body, so only
// media needs a case of its own.
static QString previewOf(const QVariantMap &entry) {
    const QVariantMap m = entry.value(QStringLiteral("last_message")).toMap();
    if (m.isEmpty())
        return {};
    QString text;
    if (m.value(QStringLiteral("retracted")).toBool()) {
        text = ChatListModel::tr("Message deleted");
    } else {
        const QVariantMap c = m.value(QStringLiteral("content")).toMap();
        if (c.value(QStringLiteral("type")).toString() == QLatin1String("media")) {
            text = c.value(QStringLiteral("caption")).toString();
            if (text.trimmed().isEmpty())
                text = attachmentPreview(
                    c.value(QStringLiteral("attachments")).toList());
        } else {
            text = c.value(QStringLiteral("body")).toString();
        }
    }
    // The row elides what is left; the cap only spares it laying out a whole
    // pasted log to show its first forty characters.
    // Don't split a surrogate pair.
    int cap = 200;
    if (text.size() > cap && text.at(cap - 1).isHighSurrogate())
        --cap;
    text = text.left(cap).simplified();
    if (text.isEmpty())
        return {};

    QString who;
    if (m.value(QStringLiteral("is_outgoing")).toBool()) {
        if (!entry.value(QStringLiteral("is_self")).toBool())
            who = ChatListModel::tr("You");
    } else if (entry.value(QStringLiteral("groupchat")).toBool()) {
        const QString from = m.value(QStringLiteral("from_jid")).toString();
        const int slash = from.indexOf(QLatin1Char('/'));
        if (slash >= 0)
            who = from.mid(slash + 1);
    }
    //: Chat list preview: who sent the newest message, then what it said
    return who.isEmpty() ? text : ChatListModel::tr("%1: %2").arg(who, text);
}

QVariantMap ChatListModel::withPreview(QVariantMap entry) {
    entry.insert(QStringLiteral("preview"), previewOf(entry));
    return entry;
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
        m_items.append(withPreview(v.toMap()));
    std::sort(m_items.begin(), m_items.end(), &ChatListModel::lessThan);
    endResetModel();
}

void ChatListModel::applyItem(const QVariantMap &item) {
    const QVariantMap entry = withPreview(item);
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

    // Where it belongs among the *other* rows, if the sort key moved.
    int target = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (i == old)
            continue;
        if (lessThan(entry, m_items.at(i)))
            break;
        ++target;
    }
    // A move keeps a view's current row on the chat. The destination counts the
    // moving row still in place.
    if (target != old) {
        beginMoveRows({}, old, old, {}, target > old ? target + 1 : target);
        m_items.move(old, target);
        endMoveRows();
    }
    m_items[target] = entry;
    const QModelIndex idx = index(target);
    emit dataChanged(idx, idx);
}

void ChatListModel::applyRemove(const QString &jid) {
    const int i = indexOfJid(jid);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_items.removeAt(i);
    endRemoveRows();
}
