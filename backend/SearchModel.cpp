#include "SearchModel.h"

#include "MessageMarkup.h"

namespace {

// One page. Smaller than the chat window's, since results are scanned rather
// than read and the next page is one step away.
constexpr int kPageSize = 30;

// text carries `body`, media a `caption`; the same union ChatModel reads. A
// retracted message never matches, so there is no tombstone case here.
QString bodyOf(const QVariantMap &m) {
    const QVariantMap c = m.value(QStringLiteral("content")).toMap();
    if (c.value(QStringLiteral("type")).toString() == QLatin1String("media"))
        return c.value(QStringLiteral("caption")).toString();
    return c.value(QStringLiteral("body")).toString();
}

// Where the query matched, from the backend that matched it. Empty where the
// hit is not in the string this draws - an attachment URL, or a stem the
// archive matched on that the text does not contain literally.
QVariantList matchesOf(const QVariantMap &m) {
    return m.value(QStringLiteral("content"))
        .toMap()
        .value(QStringLiteral("matches"))
        .toList();
}

} // namespace

SearchModel::SearchModel(QObject *parent) : QAbstractListModel(parent) {
    // Two shell windows searching the same account must not cancel each other,
    // and tacky keys cancellation by tag alone.
    static int seq = 0;
    m_tag = QStringLiteral("quack/search/%1").arg(++seq);
}

SearchModel::~SearchModel() { cancel(); }

int SearchModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_msgs.size();
}

QVariant SearchModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_msgs.size())
        return {};
    const QVariantMap &m = m_msgs.at(index.row());
    switch (role) {
    case TimestampRole: return m.value(QStringLiteral("timestamp"));
    case ChatJidRole:   return m.value(QStringLiteral("chat_jid")).toString();
    case FromRole:      return m.value(QStringLiteral("from_jid")).toString();
    case OutgoingRole:  return m.value(QStringLiteral("is_outgoing"));
    case BodyRole:      return bodyOf(m);
    case SnippetRole:   return searchSnippet(bodyOf(m), matchesOf(m));
    case MatchesRole:   return matchesOf(m);
    case RawRole:       return m;
    default:            return {};
    }
}

QHash<int, QByteArray> SearchModel::roleNames() const {
    return {
        {TimestampRole, "timestamp"},
        {ChatJidRole, "chatJid"},
        {FromRole, "from"},
        {OutgoingRole, "outgoing"},
        {BodyRole, "body"},
        {SnippetRole, "snippet"},
        {MatchesRole, "matches"},
        {RawRole, "raw"},
    };
}

void SearchModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        connect(m_backend, &TackyBackend::result, this, &SearchModel::handleResult);
    emit backendChanged();
    reset();
    askRemoteSupport();
}

void SearchModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    cancel();
    m_account = acc;
    emit accountChanged();
    reset();
    askRemoteSupport();
}

void SearchModel::setChat(const QString &chat) {
    if (m_chat == chat)
        return;
    cancel();
    m_chat = chat;
    emit chatChanged();
    reset();
    askRemoteSupport();
}

void SearchModel::setQuery(const QString &query) {
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
}

void SearchModel::setAlsoRemote(bool v) {
    if (m_alsoRemote == v)
        return;
    m_alsoRemote = v;
    emit alsoRemoteChanged();
}

void SearchModel::search() {
    if (!m_backend || m_account.isEmpty() || m_query.isEmpty())
        return;
    cancel();
    reset();
    m_matched = m_query;
    m_searched = true;
    emit searchedChanged();
    issue(false);
}

void SearchModel::loadMore() {
    if (searching() || m_complete || m_cursor.isEmpty() || m_matched.isEmpty())
        return;
    issue(true);
}

void SearchModel::clear() {
    cancel();
    reset();
}

qlonglong SearchModel::timestampAt(int row) const {
    if (row < 0 || row >= m_msgs.size())
        return 0;
    return m_msgs.at(row).value(QStringLiteral("timestamp")).toLongLong();
}

QVariantList SearchModel::matchesAt(int row) const {
    if (row < 0 || row >= m_msgs.size())
        return {};
    return matchesOf(m_msgs.at(row));
}

// A page and the source it comes from. Only the first leg may go to the server:
// tacky's `both` skips its remote half the moment a cursor is passed, and hands
// back a store cursor either way, so paging that asked for `both` would be a
// local page wearing the wrong label.
void SearchModel::issue(bool append) {
    QVariantMap a{{QStringLiteral("acc"), m_account},
                  {QStringLiteral("query"), m_matched},
                  {QStringLiteral("limit"), kPageSize},
                  {QStringLiteral("tag"), m_tag}};
    if (!m_chat.isEmpty())
        a.insert(QStringLiteral("chat"), m_chat);
    const bool remoteLeg = !append && m_alsoRemote && m_remoteAvailable;
    a.insert(QStringLiteral("source"),
             remoteLeg ? QStringLiteral("both") : QStringLiteral("local"));
    if (append)
        a.insert(QStringLiteral("before"), m_cursor);

    m_appending = append;
    m_token = m_backend->request(QStringLiteral("message"),
                                 QStringLiteral("search"), a);
    emit searchingChanged();
}

void SearchModel::cancel() {
    if (m_backend && !m_account.isEmpty())
        m_backend->notify(QStringLiteral("message"), QStringLiteral("cancel"),
                          QVariantMap{{QStringLiteral("acc"), m_account},
                                      {QStringLiteral("tag"), m_tag}});
    clearInflight(); // drops the token too, so a late reply is ignored
}

void SearchModel::clearInflight() {
    if (m_token == 0)
        return;
    m_token = 0;
    emit searchingChanged();
}

void SearchModel::reset() {
    if (!m_msgs.isEmpty()) {
        beginResetModel();
        m_msgs.clear();
        endResetModel();
        emit countChanged();
    }
    rebuildResultChats();
    m_matched.clear();
    m_cursor.clear();
    setComplete(false);
    if (m_searched) {
        m_searched = false;
        emit searchedChanged();
    }
    if (m_failed) {
        m_failed = false;
        emit failedChanged();
    }
}

// Whether a server-side leg is worth offering at all. Account-wide there is no
// single archive to ask, so the question does not arise.
void SearchModel::askRemoteSupport() {
    m_capsToken = 0;
    setRemoteAvailable(false);
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    m_capsToken = m_backend->request(
        QStringLiteral("mam"), QStringLiteral("fulltextSupported"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("chat"), m_chat}});
}

void SearchModel::handleResult(int token, const QVariant &data) {
    if (m_capsToken != 0 && token == m_capsToken) {
        m_capsToken = 0;
        setRemoteAvailable(data.toBool());
        return;
    }
    if (m_token == 0 || token != m_token)
        return;
    const bool append = m_appending;
    clearInflight();
    applyResult(data.toMap(), append);
}

// `error` and `unsupported` sit outside the declared JSON schema, so they come
// over as the string "1" rather than a bool; QVariant reads either as true.
void SearchModel::applyResult(const QVariantMap &result, bool append) {
    if (result.value(QStringLiteral("error")).toBool()) {
        m_failed = true;
        emit failedChanged();
        setComplete(true);
        return;
    }

    const QVariantList messages = result.value(QStringLiteral("messages")).toList();
    const int was = m_msgs.size();
    if (!append && !m_msgs.isEmpty()) {
        beginResetModel();
        m_msgs.clear();
        endResetModel();
    }
    if (!messages.isEmpty()) {
        const int at = m_msgs.size();
        beginInsertRows({}, at, at + messages.size() - 1);
        for (const QVariant &v : messages)
            m_msgs.append(v.toMap());
        endInsertRows();
    }
    if (m_msgs.size() != was)
        emit countChanged();

    // Verbatim: scoped this is a timestamp, account-wide a {timestamp chat_jid}
    // pair, and only tacky has to be able to tell them apart.
    m_cursor = result.value(QStringLiteral("last")).toString();
    setComplete(result.value(QStringLiteral("complete")).toBool() ||
                m_cursor.isEmpty());
    rebuildResultChats();
}

void SearchModel::setComplete(bool v) {
    if (m_complete == v)
        return;
    m_complete = v;
    emit completeChanged();
}

void SearchModel::setRemoteAvailable(bool v) {
    if (m_remoteAvailable == v)
        return;
    m_remoteAvailable = v;
    emit remoteAvailableChanged();
}

void SearchModel::rebuildResultChats() {
    QStringList chats;
    for (const QVariantMap &m : std::as_const(m_msgs)) {
        const QString jid = m.value(QStringLiteral("chat_jid")).toString();
        if (!jid.isEmpty() && !chats.contains(jid))
            chats.append(jid);
    }
    if (chats == m_resultChats)
        return;
    m_resultChats = chats;
    emit resultChatsChanged();
}
