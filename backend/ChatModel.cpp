#include "ChatModel.h"

#include "MessageMarkup.h"
#include "TackyBackend.h"

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

// text carries `body`, media a `caption` (possibly "" for a bare share). Only
// the text is surfaced; attachments want a media delegate we don't have yet.
static QString bodyOf(const QVariantMap &m) {
    if (m.value(QStringLiteral("retracted")).toBool())
        return {};
    const QVariantMap c = m.value(QStringLiteral("content")).toMap();
    if (c.value(QStringLiteral("type")).toString() == QLatin1String("media"))
        return c.value(QStringLiteral("caption")).toString();
    return c.value(QStringLiteral("body")).toString();
}

// The spans index into whichever string bodyOf returned, so both come from the
// same content variant. A retraction empties that body, and an empty body needs
// no markup, so the tombstone falls out without a case of its own.
static QString markupOf(const QVariantMap &m, const QString &quoteColor) {
    return messageMarkup(bodyOf(m),
                         m.value(QStringLiteral("content"))
                             .toMap()
                             .value(QStringLiteral("formatting"))
                             .toList(),
                         quoteColor);
}

int ChatModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_msgs.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_msgs.size())
        return {};
    const QVariantMap &m = m_msgs.at(index.row());
    switch (role) {
    case TimestampRole:    return m.value(QStringLiteral("timestamp"));
    case BodyRole:         return bodyOf(m);
    case MarkupRole:       return markupOf(m, m_quoteColor);
    case OutgoingRole:     return m.value(QStringLiteral("is_outgoing"));
    case ServerStatusRole: return m.value(QStringLiteral("server_status"));
    case FromRole:         return m.value(QStringLiteral("from_jid"));
    case RetractedRole:    return m.value(QStringLiteral("retracted"));
    case ReplyBodyRole:    return m.value(QStringLiteral("reply_body"));
    case ReplyAuthorRole:  return m.value(QStringLiteral("reply_author_jid"));
    case RawRole:          return m;
    default:               return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        {TimestampRole, "timestamp"},
        {BodyRole, "body"},
        {MarkupRole, "markup"},
        {OutgoingRole, "outgoing"},
        {ServerStatusRole, "serverStatus"},
        {FromRole, "from"},
        {RetractedRole, "retracted"},
        {ReplyBodyRole, "replyBody"},
        {ReplyAuthorRole, "replyAuthor"},
        {RawRole, "raw"},
    };
}

void ChatModel::setAtTail(bool v) {
    if (m_atTail == v)
        return;
    m_atTail = v;
    emit atTailChanged();
}

void ChatModel::setCatchupBusy(bool v) {
    if (m_catchupBusy == v)
        return;
    m_catchupBusy = v;
    emit catchupBusyChanged();
}

// One door in and out of m_inflight, so loadingOlder cannot drift from it.
void ChatModel::markInflight(const QString &dir, bool busy) {
    const bool was = loadingOlder();
    if (busy)
        m_inflight.insert(dir);
    else
        m_inflight.remove(dir);
    if (was != loadingOlder())
        emit loadingOlderChanged();
}

qlonglong ChatModel::oldestTs() const {
    return m_msgs.isEmpty()
               ? 0
               : m_msgs.last().value(QStringLiteral("timestamp")).toLongLong();
}

qlonglong ChatModel::newestTs() const {
    return m_msgs.isEmpty()
               ? 0
               : m_msgs.first().value(QStringLiteral("timestamp")).toLongLong();
}

int ChatModel::indexOfTs(qlonglong ts) const {
    for (int i = 0; i < m_msgs.size(); ++i)
        if (m_msgs.at(i).value(QStringLiteral("timestamp")).toLongLong() == ts)
            return i;
    return -1;
}

int ChatModel::insertPos(qlonglong ts) const {
    // Descending: newest (largest ts) at row 0.
    for (int i = 0; i < m_msgs.size(); ++i)
        if (m_msgs.at(i).value(QStringLiteral("timestamp")).toLongLong() < ts)
            return i;
    return m_msgs.size();
}

void ChatModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::event, this, &ChatModel::handleEvent);
        connect(m_backend, &TackyBackend::result, this, &ChatModel::handleResult);
    }
    emit backendChanged();
    reload();
}

void ChatModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    reload();
}

void ChatModel::setChat(const QString &chat) {
    if (m_chat == chat)
        return;
    m_chat = chat;
    emit chatChanged();
    reload();
}

// Only picks which catchup bracket we listen to, so no reload. QML sets it
// alongside `chat` in either order; events can't arrive between two property
// writes, so neither order can miss a bracket.
void ChatModel::setGroupchat(bool v) {
    if (m_groupchat == v)
        return;
    m_groupchat = v;
    emit groupchatChanged();
}

// Baked into the markup, so every row that carries one has to be redrawn -
// Ctrl+T swaps the palette under an open chat.
void ChatModel::setQuoteColor(const QString &css) {
    if (m_quoteColor == css)
        return;
    m_quoteColor = css;
    emit quoteColorChanged();
    if (!m_msgs.isEmpty())
        emit dataChanged(index(0), index(m_msgs.size() - 1), {MarkupRole});
}

// atTail stays true: an empty window is vacuously at tail, and a live event
// arriving before the initial page lands gets deduped by it.
void ChatModel::reload() {
    cancelDir(QStringLiteral("old"));
    cancelDir(QStringLiteral("new"));
    cancelDir(QStringLiteral("goto"));
    cancelDir(QStringLiteral("catchup"));
    if (!m_msgs.isEmpty()) {
        beginResetModel();
        m_msgs.clear();
        endResetModel();
    }
    setAtTail(true);
    // Any open bracket belonged to the previous chat; the new one's own
    // <CatchupStarted> re-raises the flag if a sync is running for it.
    setCatchupBusy(false);
    m_tailTs = 0; // the last <Tail> was the previous chat's
    loadInitial();
}

void ChatModel::issueHistory(const QString &dir, qlonglong cursor,
                             bool haveCursor) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    if (m_inflight.contains(dir))
        return; // one request per direction in flight

    QVariantMap a{{QStringLiteral("acc"), m_account},
                  {QStringLiteral("chat"), m_chat},
                  {QStringLiteral("limit"), 50},
                  {QStringLiteral("tag"), m_chat + QLatin1Char('/') + dir}};
    if (haveCursor)
        a.insert(dir == QLatin1String("old") ? QStringLiteral("before")
                                             : QStringLiteral("after"),
                 cursor);
    const int tok = m_backend->request(QStringLiteral("message"),
                                       QStringLiteral("history"), a);
    m_pending.insert(tok, dir);
    markInflight(dir, true);
}

void ChatModel::loadInitial() { issueHistory(QStringLiteral("init"), 0, false); }

void ChatModel::loadOlder() {
    if (m_msgs.isEmpty())
        return;
    issueHistory(QStringLiteral("old"), oldestTs(), true);
}

void ChatModel::loadNewer() {
    if (m_msgs.isEmpty())
        return;
    issueHistory(QStringLiteral("new"), newestTs(), true);
}

// Both jumps land in the same reply handler, which decides then whether the
// window has to move at all - so neither leaves the tail up front. A target
// already on screen only needs scrolling to.
void ChatModel::issueGoto(const QString &method, const QVariantMap &args) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    cancelDir(QStringLiteral("goto"));
    cancelDir(QStringLiteral("old"));
    cancelDir(QStringLiteral("new"));
    cancelDir(QStringLiteral("catchup"));
    QVariantMap a = args;
    a.insert(QStringLiteral("acc"), m_account);
    a.insert(QStringLiteral("chat"), m_chat);
    a.insert(QStringLiteral("limit"), 50);
    a.insert(QStringLiteral("tag"), m_chat + QStringLiteral("/goto"));
    const int tok = m_backend->request(QStringLiteral("message"), method, a);
    m_pending.insert(tok, QStringLiteral("goto"));
    markInflight(QStringLiteral("goto"), true);
}

void ChatModel::gotoTimestamp(qlonglong ts, const QString &source) {
    issueGoto(QStringLiteral("goto"), {{QStringLiteral("date"), ts},
                                       {QStringLiteral("source"), source}});
}

void ChatModel::gotoReplyTarget(qlonglong ts) {
    const int row = indexOfTs(ts);
    if (row < 0)
        return;
    const QVariantMap m = m_msgs.at(row);
    const QString id = m.value(QStringLiteral("reply_id")).toString();
    if (id.isEmpty()) {
        emit anchored(0);
        return;
    }
    issueGoto(QStringLiteral("gotoReply"),
              {{QStringLiteral("reply_id"), id},
               {QStringLiteral("reply_to"), m.value(QStringLiteral("reply_to"))}});
}

void ChatModel::resetToBottom() {
    cancelDir(QStringLiteral("old"));
    cancelDir(QStringLiteral("new"));
    cancelDir(QStringLiteral("goto"));
    cancelDir(QStringLiteral("catchup"));
    if (!m_msgs.isEmpty()) {
        beginResetModel();
        m_msgs.clear();
        endResetModel();
    }
    setAtTail(false); // back to true when the initial page lands
    loadInitial();
}

// tacky builds the XEP-0461 reference and the "> " fallback quote from the
// target's own row, so the timestamp is the whole of what we send: the body
// stays the text the user typed, unquoted, both on the wire and in the store.
void ChatModel::send(const QString &body, qlonglong replyToTs) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty() || body.isEmpty())
        return;
    QVariantMap a{{QStringLiteral("acc"), m_account},
                  {QStringLiteral("chat"), m_chat},
                  {QStringLiteral("body"), body}};
    if (replyToTs != 0)
        a.insert(QStringLiteral("reply_to_ts"), replyToTs);
    m_backend->notify(QStringLiteral("message"), QStringLiteral("send"), a);
}

void ChatModel::cullOld(int count) {
    const int n = qMin(count, m_msgs.size());
    if (n <= 0)
        return;
    // Oldest rows are at the bottom (largest indices).
    beginRemoveRows({}, m_msgs.size() - n, m_msgs.size() - 1);
    m_msgs.remove(m_msgs.size() - n, n);
    endRemoveRows();
    cancelDir(QStringLiteral("old")); // its cursor no longer touches the window
}

void ChatModel::cullNew(int count) {
    const int n = qMin(count, m_msgs.size());
    if (n <= 0)
        return;
    // Newest rows are at the top (row 0).
    beginRemoveRows({}, 0, n - 1);
    m_msgs.remove(0, n);
    endRemoveRows();
    cancelDir(QStringLiteral("new"));
    cancelDir(QStringLiteral("catchup")); // its after-cursor left the window too
    setAtTail(false);
}

void ChatModel::cancelDir(const QString &dir) {
    if (m_backend && !m_account.isEmpty())
        m_backend->notify(
            QStringLiteral("message"), QStringLiteral("cancel"),
            QVariantMap{{QStringLiteral("acc"), m_account},
                        {QStringLiteral("tag"), m_chat + QLatin1Char('/') + dir}});
    markInflight(dir, false);
    // Drop the pending token too, so a late reply is ignored.
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it.value() == dir)
            it = m_pending.erase(it);
        else
            ++it;
    }
}

// Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
void ChatModel::handleEvent(const QString &module, const QString &name,
                            const QVariant &args) {
    if (module != QLatin1String("message"))
        return;
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account)
        return;
    const QString jid = a.value(QStringLiteral("jid")).toString();

    // Catchup brackets have their own jid rule, so they must run before the
    // own-chat filter below would drop the empty-jid account bracket.
    if (name == QLatin1String("CatchupStarted")) {
        if (isMyCatchup(jid))
            setCatchupBusy(true);
        return;
    }
    if (name == QLatin1String("CatchupDone")) {
        if (!isMyCatchup(jid))
            return;
        setCatchupBusy(false);
        reconcileCatchup();
        return;
    }

    if (jid != m_chat)
        return;
    const qlonglong ts = a.value(QStringLiteral("timestamp")).toLongLong();

    if (name == QLatin1String("New")) {
        // Inserting while the window is off the tail would advance the "new"
        // cursor past an unfetched run, leaving a permanent gap. An open catchup
        // bracket is the same problem: the archive may hold history we don't,
        // so atTail is a claim we can't check until reconcileCatchup runs.
        // Dropped events are durable in the store and refetched.
        if (!m_atTail || m_catchupBusy)
            return;
        applyBatch(QVariantList{a.value(QStringLiteral("message"))});
    } else if (name == QLatin1String("Edited")) {
        // An edit re-sends the whole row; upsert redraws it in place.
        applyBatch(QVariantList{a.value(QStringLiteral("message"))});
    } else if (name == QLatin1String("Status")) {
        QVariantMap f = a;
        f.remove(QStringLiteral("acc"));
        f.remove(QStringLiteral("jid"));
        f.remove(QStringLiteral("timestamp"));
        applyFields(ts, f);
    } else if (name == QLatin1String("Reactions")) {
        applyFields(ts, QVariantMap{{QStringLiteral("reactions"),
                                     a.value(QStringLiteral("reactions"))}});
    } else if (name == QLatin1String("Confirmed")) {
        applyConfirmed(ts, a.value(QStringLiteral("newtimestamp")).toLongLong(),
                       a.value(QStringLiteral("server_status")).toString());
    } else if (name == QLatin1String("Retracted")) {
        applyRetracted(ts);
    } else if (name == QLatin1String("Tail")) {
        // The chat's newest real-message ts; drives the at-tail rejoin check.
        m_tailTs = ts;
        if (!m_atTail && !m_msgs.isEmpty() && newestTs() == m_tailTs)
            setAtTail(true);
    }
}

// Ours when the bracket names this chat, or when it's the account-wide one
// (empty jid) and we're a 1:1 - the account archive carries no groupchat.
bool ChatModel::isMyCatchup(const QString &jid) const {
    if (jid == m_chat)
        return true;
    return jid.isEmpty() && !m_groupchat;
}

// Catchup stores without emitting <New>, and anything live that landed
// mid-sync was dropped, so reconcile against the pushed tail. Off-tail windows
// need nothing: their forward paging reads the freshly synced store anyway.
void ChatModel::reconcileCatchup() {
    if (!m_atTail)
        return;
    if (m_inflight.contains(QStringLiteral("new")) ||
        m_inflight.contains(QStringLiteral("catchup")))
        return; // a forward page in flight already covers this span
    if (m_msgs.isEmpty()) {
        loadInitial();
        return;
    }
    if (m_tailTs != 0 && newestTs() == m_tailTs)
        return;
    issueHistory(QStringLiteral("catchup"), newestTs(), true);
}

void ChatModel::handleResult(int token, const QVariant &data) {
    if (!m_pending.contains(token))
        return;
    const QString role = m_pending.take(token);
    markInflight(role, false);

    const int before = m_msgs.size();
    if (role == QLatin1String("init")) {
        applyBatch(data.toList());
        setAtTail(true); // the newest page contains the tail by definition
    } else if (role == QLatin1String("old")) {
        applyBatch(data.toList());
    } else if (role == QLatin1String("new")) {
        applyBatch(data.toList());
        if (m_tailTs != 0 && !m_msgs.isEmpty() && newestTs() == m_tailTs)
            setAtTail(true);
    } else if (role == QLatin1String("catchup")) {
        // A reconcile page whose newest stops short of the tail has a hole
        // after it; appending would walk the user into old history without
        // going live, so discard it and leave the scroll-to-bottom path.
        const QVariantList page = data.toList();
        qlonglong pageNewest = 0;
        for (const QVariant &v : page)
            pageNewest = qMax(pageNewest,
                              v.toMap().value(QStringLiteral("timestamp")).toLongLong());
        if (page.isEmpty() || pageNewest == m_tailTs)
            applyBatch(page);
    } else if (role == QLatin1String("goto")) {
        // An anchor already on screen means the window stays as it is, tail
        // and all; an unresolved one (empty anchor) means nothing to go to.
        const QVariantMap res = data.toMap();
        const qlonglong anchor = res.value(QStringLiteral("anchor")).toLongLong();
        if (anchor != 0 && indexOfTs(anchor) < 0)
            applyGotoSlice(res.value(QStringLiteral("messages")).toList());
        emit anchored(anchor);
    }
    // goto resets the window, so its delta isn't a plain insert count; the view
    // only fills on init/old and ignores goto's number.
    emit loaded(role, m_msgs.size() - before);
}

void ChatModel::applyBatch(const QVariantList &messages) {
    for (const QVariant &v : messages) {
        const QVariantMap msg = v.toMap();
        const qlonglong ts = msg.value(QStringLiteral("timestamp")).toLongLong();
        if (ts == 0)
            continue;
        const int existing = indexOfTs(ts);
        if (existing >= 0) { // already displayed: patch in place
            QVariantMap merged = m_msgs.at(existing);
            for (auto it = msg.begin(); it != msg.end(); ++it)
                merged.insert(it.key(), it.value());
            m_msgs[existing] = merged;
            const QModelIndex idx = index(existing);
            emit dataChanged(idx, idx);
            continue;
        }
        const int pos = insertPos(ts);
        beginInsertRows({}, pos, pos);
        m_msgs.insert(pos, msg);
        endInsertRows();
    }
}

// Merge fields into the row at ts; dropped if that row isn't displayed.
void ChatModel::applyFields(qlonglong ts, const QVariantMap &fields) {
    const int idx = indexOfTs(ts);
    if (idx < 0)
        return;
    QVariantMap merged = m_msgs.at(idx);
    for (auto it = fields.begin(); it != fields.end(); ++it)
        merged.insert(it.key(), it.value());
    m_msgs[idx] = merged;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
}

// A pending send the server acknowledged. When the server relocated the row
// (newTs != ts) this rekeys and re-sorts - the only event that moves a message.
void ChatModel::applyConfirmed(qlonglong ts, qlonglong newTs,
                               const QString &serverStatus) {
    const int idx = indexOfTs(ts);
    if (idx < 0)
        return;
    if (newTs == 0 || newTs == ts) {
        applyFields(ts, QVariantMap{{QStringLiteral("server_status"), serverStatus}});
        return;
    }
    QVariantMap moved = m_msgs.at(idx);
    beginRemoveRows({}, idx, idx);
    m_msgs.removeAt(idx);
    endRemoveRows();
    moved.insert(QStringLiteral("timestamp"), newTs);
    moved.insert(QStringLiteral("server_status"), serverStatus);
    applyBatch(QVariantList{moved});
}

// Flip the row to a tombstone: keep it for pagination/anchoring, drop content.
void ChatModel::applyRetracted(qlonglong ts) {
    const int idx = indexOfTs(ts);
    if (idx < 0)
        return;
    QVariantMap m = m_msgs.at(idx);
    m.insert(QStringLiteral("retracted"), true);
    m.remove(QStringLiteral("content"));
    m_msgs[idx] = m;
    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
}

void ChatModel::applyGotoSlice(const QVariantList &messages) {
    beginResetModel();
    m_msgs.clear();
    endResetModel();
    setAtTail(false); // a goto slice may not reach the tail
    applyBatch(messages);
}
