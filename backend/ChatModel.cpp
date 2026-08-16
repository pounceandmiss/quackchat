#include "ChatModel.h"

#include <QFile>
#include <QFileInfo>

#include "MessageMarkup.h"
#include "MessageXml.h"
#include "PickedFile.h"
#include "TackyBackend.h"

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent) {}

// text carries `body`, media a `caption` (possibly "" for a bare share). The
// attachments themselves come out through AttachmentsRole.
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
//
// A search mark is one more span over the same string, in the same units, so it
// joins the styling rather than competing with it.
static QString markupOf(const QVariantMap &m, const QString &quoteColor,
                        const QString &matchColor,
                        const QVariantList &matches) {
    QVariantList spans = m.value(QStringLiteral("content"))
                             .toMap()
                             .value(QStringLiteral("formatting"))
                             .toList();
    for (const QVariant &v : matches) {
        QVariantMap span = v.toMap();
        span.insert(QStringLiteral("type"), QStringLiteral("match"));
        spans.append(span);
    }
    return messageMarkup(bodyOf(m), spans, quoteColor, matchColor);
}

// An attachment nothing has happened to yet. Spelled out rather than left
// absent so the delegate binds to every key without a guard: a missing QVariant
// reaches QML as undefined.
static QVariantMap idleTransfer() {
    return {{QStringLiteral("state"), QString()},
            {QStringLiteral("direction"), QString()},
            {QStringLiteral("loaded"), 0},
            {QStringLiteral("total"), 0},
            {QStringLiteral("localpath"), QString()},
            {QStringLiteral("thumburl"), QUrl()},
            {QStringLiteral("error"), QString()}};
}

// The row carries what the message said; m_xfer and m_upload carry what has
// happened to it since. Merged here so the view binds to one list.
//
// The upload goes on last, so a picture on its way out shows its own bytes
// rather than the instant local read that produced its thumbnail.
QVariantList ChatModel::attachmentsOf(const QVariantMap &m) const {
    QVariantList out;
    if (m.value(QStringLiteral("retracted")).toBool())
        return out;
    const QVariantList atts = m.value(QStringLiteral("content"))
                                  .toMap()
                                  .value(QStringLiteral("attachments"))
                                  .toList();
    const QVariantMap up =
        m_upload.value(m.value(QStringLiteral("timestamp")).toLongLong());
    for (const QVariant &v : atts) {
        QVariantMap a = idleTransfer();
        const QVariantMap said = v.toMap();
        for (auto it = said.begin(); it != said.end(); ++it)
            a.insert(it.key(), it.value());
        const QVariantMap x = m_xfer.value(a.value(QStringLiteral("url")).toString());
        for (auto it = x.begin(); it != x.end(); ++it)
            a.insert(it.key(), it.value());
        for (auto it = up.begin(); it != up.end(); ++it)
            a.insert(it.key(), it.value());
        out.append(a);
    }
    return out;
}

QVariantMap ChatModel::attachmentAt(qlonglong ts, int idx) const {
    const int row = indexOfTs(ts);
    if (row < 0)
        return {};
    const QVariantList atts = attachmentsOf(m_msgs.at(row));
    if (idx < 0 || idx >= atts.size())
        return {};
    return atts.at(idx).toMap();
}

// Every message tacky sends carries its stanza, so this reads the row rather
// than asking the backend a second time. Laid out on the way past, as
// MarkupRole hands over a body the view can draw as it stands.
QString ChatModel::rawXml(qlonglong ts) const {
    const int row = indexOfTs(ts);
    if (row < 0)
        return {};
    return formatMessageXml(
        m_msgs.at(row).value(QStringLiteral("raw_xml")).toString());
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
    case MarkupRole:       return markupOf(m, m_quoteColor, m_matchColor, marksOn(m));
    case AttachmentsRole:  return attachmentsOf(m);
    case OutgoingRole:     return m.value(QStringLiteral("is_outgoing"));
    case ServerStatusRole:
        return m.value(QStringLiteral("server_status")).toString();
    case RemoteStatusRole:
        return m.value(QStringLiteral("remote_status")).toString();
    case FromRole:         return m.value(QStringLiteral("from_jid"));
    case RetractedRole:    return m.value(QStringLiteral("retracted"));
    case ReactionsRole:    return m.value(QStringLiteral("reactions")).toMap();
    // Coerced, not passed through: these keys are absent on an ordinary
    // message, and a missing QVariant reaches QML as undefined, which a string
    // property renders as the word "undefined" instead of nothing.
    case ReplyBodyRole:    return m.value(QStringLiteral("reply_body")).toString();
    case ReplyAuthorRole:
        return m.value(QStringLiteral("reply_author_jid")).toString();
    // Intent, not outcome: a row that was meant to go out encrypted keeps this
    // even when the encryption never came off, which is what tells a failure
    // "could not encrypt" apart from "could not deliver".
    case EncryptionRole:   return m.value(QStringLiteral("encryption")).toString();
    case FailReasonRole:   return m.value(QStringLiteral("fail_reason")).toString();
    case RawRole:          return m;
    default:               return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        {TimestampRole, "timestamp"},
        {BodyRole, "body"},
        {MarkupRole, "markup"},
        {AttachmentsRole, "attachments"},
        {OutgoingRole, "outgoing"},
        {ServerStatusRole, "serverStatus"},
        {RemoteStatusRole, "remoteStatus"},
        {FromRole, "from"},
        {RetractedRole, "retracted"},
        {ReactionsRole, "reactions"},
        {ReplyBodyRole, "replyBody"},
        {ReplyAuthorRole, "replyAuthor"},
        {EncryptionRole, "encryption"},
        {FailReasonRole, "failReason"},
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
        connect(m_backend, &TackyBackend::connected, this, &ChatModel::reload);
        connect(m_backend, &TackyBackend::error, this, &ChatModel::handleError);
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

// The search marks one message at a time; every other row draws without them.
QVariantList ChatModel::marksOn(const QVariantMap &m) const {
    return m.value(QStringLiteral("timestamp")).toLongLong() == m_matchTs
               ? m_matchRanges
               : QVariantList();
}

void ChatModel::setMatchColor(const QString &css) {
    if (m_matchColor == css)
        return;
    m_matchColor = css;
    emit matchColorChanged();
    const int row = indexOfTs(m_matchTs);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {MarkupRole});
}

// Later fetches only; one already rendered stays the size it was.
void ChatModel::setThumbMax(int px) {
    if (px <= 0 || m_thumbMax == px)
        return;
    m_thumbMax = px;
    emit thumbMaxChanged();
}

// Only one message wears the mark, so moving it repaints where it was as well
// as where it goes - and a row off the window simply has nothing to repaint.
void ChatModel::highlightMatches(qlonglong ts, const QVariantList &ranges) {
    const int was = indexOfTs(m_matchTs);
    m_matchTs = ranges.isEmpty() ? 0 : ts;
    m_matchRanges = ranges;
    const int now = indexOfTs(m_matchTs);
    if (was >= 0)
        emit dataChanged(index(was), index(was), {MarkupRole});
    if (now >= 0 && now != was)
        emit dataChanged(index(now), index(now), {MarkupRole});
}

// atTail stays true: an empty window is vacuously at tail, and a live event
// arriving before the initial page lands gets deduped by it.
void ChatModel::reload() {
    cancelAllDirs();
    if (!m_msgs.isEmpty()) {
        beginResetModel();
        m_msgs.clear();
        endResetModel();
    }
    // The transfers belonged to the rows we just dropped. A re-request for one
    // of their urls is answered from tacky's cache, so nothing is lost.
    m_xfer.clear();
    m_upload.clear();
    m_pendingAction.clear();
    setAtTail(true);
    // Any open bracket belonged to the previous chat; the new one's own
    // <CatchupStarted> re-raises the flag if a sync is running for it.
    setCatchupBusy(false);
    m_tailTs = 0;      // the last <Tail> was the previous chat's
    m_markedRead = 0;  // and so was the watermark we last sent
    m_failed.clear();
    setLoadError({});
    pullConnState();
    loadInitial();
}

void ChatModel::issueHistory(const QString &dir, qlonglong cursor,
                             bool haveCursor) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    if (m_inflight.contains(dir))
        return; // one request per direction in flight
    clearFailure(dir);

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

// The fill calls these on every content change, so a failed direction stays
// quiet: an unreachable archive would otherwise draw a request per scroll.
void ChatModel::loadOlder() {
    if (m_msgs.isEmpty() || m_failed.contains(QStringLiteral("old")))
        return;
    issueHistory(QStringLiteral("old"), oldestTs(), true);
}

void ChatModel::loadNewer() {
    if (m_msgs.isEmpty() || m_failed.contains(QStringLiteral("new")))
        return;
    issueHistory(QStringLiteral("new"), newestTs(), true);
}

// Straight to issueHistory: the gate above is what this overrides.
void ChatModel::retry() {
    const QSet<QString> failed = m_failed;
    if (failed.contains(QStringLiteral("init")))
        issueHistory(QStringLiteral("init"), 0, false);
    if (failed.contains(QStringLiteral("old")) && !m_msgs.isEmpty())
        issueHistory(QStringLiteral("old"), oldestTs(), true);
    if (failed.contains(QStringLiteral("new")) && !m_msgs.isEmpty())
        issueHistory(QStringLiteral("new"), newestTs(), true);
}

// Re-fires conn <State>, so a model built after the account connected
// still learns it.
void ChatModel::pullConnState() {
    if (!m_backend || m_account.isEmpty())
        return;
    m_backend->notify(QStringLiteral("conn"), QStringLiteral("pull"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("event"),
                                   QStringLiteral("State")}});
}

void ChatModel::setLoadError(const QString &message) {
    if (m_loadError == message)
        return;
    m_loadError = message;
    emit loadErrorChanged();
}

void ChatModel::setOnline(bool v) {
    if (m_online == v)
        return;
    m_online = v;
    emit onlineChanged();
}

void ChatModel::clearFailure(const QString &dir) {
    if (m_failed.remove(dir) && m_failed.isEmpty())
        setLoadError({});
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
    // A jump made straight after a chat switch races that chat's opening page:
    // both are out, and whichever lands second owns the window. The jump is the
    // more recent instruction, so the newest page gives way to it.
    cancelDir(QStringLiteral("init"));
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

// markOwnRead is forward-only and safe to repeat, but the view calls this on
// every insert and scroll, so an unchanged watermark is dropped here rather
// than turned into a frame.
void ChatModel::markRead() {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    const qlonglong ts = newestTs();
    if (ts <= 0 || ts <= m_markedRead)
        return;
    m_markedRead = ts;
    const QVariantMap args{{QStringLiteral("acc"), m_account},
                           {QStringLiteral("chat"), m_chat},
                           {QStringLiteral("timestamp"), ts}};
    m_backend->notify(QStringLiteral("message"), QStringLiteral("markOwnRead"),
                      args);
    // The wire half of the same read, 1:1 only (XEP-0333 <displayed>).
    if (!m_groupchat)
        m_backend->notify(QStringLiteral("message"),
                          QStringLiteral("markDisplayed"), args);
}

void ChatModel::resetToBottom() {
    cancelAllDirs();
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

// Whether the file is encrypted before the PUT follows the chat's own OMEMO
// switch, which tacky reads on the way past.
void ChatModel::sendFile(const QUrl &file) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    const QString path = pickedfile::localPath(file);
    if (path.isEmpty())
        return;
    m_backend->notify(QStringLiteral("message"), QStringLiteral("sendFile"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("chat"), m_chat},
                                  {QStringLiteral("path"), path}});
}

void ChatModel::retryUpload(qlonglong ts) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    m_backend->notify(QStringLiteral("message"), QStringLiteral("retryUpload"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("chat"), m_chat},
                                  {QStringLiteral("timestamp"), ts}});
}

// The aggregated map comes back as a <Reactions> event, so neither of these
// touches the row: they ask, and the backend answers with the whole set.
void ChatModel::resend(qlonglong ts, bool plaintext) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    m_backend->notify(QStringLiteral("message"), QStringLiteral("resend"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("chat"), m_chat},
                                  {QStringLiteral("timestamp"), ts},
                                  {QStringLiteral("plaintext"), plaintext ? 1 : 0}});
}

void ChatModel::react(qlonglong ts, const QString &emoji) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty() || emoji.isEmpty())
        return;
    m_backend->notify(QStringLiteral("message"), QStringLiteral("react"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("chat"), m_chat},
                                  {QStringLiteral("timestamp"), ts},
                                  {QStringLiteral("emoji"), emoji}});
}

void ChatModel::reactClear(qlonglong ts) {
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    m_backend->notify(QStringLiteral("message"), QStringLiteral("reactClear"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("chat"), m_chat},
                                  {QStringLiteral("timestamp"), ts}});
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

// "init" included: a lost reply leaves it in m_inflight, and issueHistory then
// refuses every later init, so the feed stays empty with the pill lit.
// An errored request is as gone as a lost one: without clearing the direction
// it stays in m_inflight and issueHistory refuses every retry.
void ChatModel::handleError(int token, const QString &message) {
    // Refused outright rather than answered empty-handed; the caller waiting on
    // the file is told the same either way.
    if (m_pendingAction.contains(token)) {
        finishAction(m_pendingAction.take(token), {});
        return;
    }
    const QString dir = m_pending.take(token);
    if (dir.isEmpty())
        return;
    markInflight(dir, false);
    qWarning("chat history (%s) failed: %s", qUtf8Printable(dir),
             qUtf8Printable(message));
    // No loaded(dir, 0): that tells the view the archive ran dry, and it
    // latches on it.
    m_failed.insert(dir);
    if (dir == QLatin1String("init") || dir == QLatin1String("old"))
        setLoadError(message);
}

void ChatModel::cancelAllDirs() {
    for (const char *dir : {"init", "old", "new", "goto", "catchup"})
        cancelDir(QLatin1String(dir));
}

void ChatModel::cancelDir(const QString &dir) {
    // Only worth telling the backend about a request it actually has: a chat
    // switch cancels every direction, and at most one or two are ever out.
    if (m_backend && !m_account.isEmpty() && m_inflight.contains(dir))
        m_backend->notify(
            QStringLiteral("message"), QStringLiteral("cancel"),
            QVariantMap{{QStringLiteral("acc"), m_account},
                        {QStringLiteral("tag"), m_chat + QLatin1Char('/') + dir}});
    markInflight(dir, false);
    clearFailure(dir);
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
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account)
        return;
    if (module == QLatin1String("file")) {
        handleFileUpdate(name, a);
        return;
    }
    // TackyBackend::connected is this process attaching to the backend; these
    // are the account reaching its server, which is what a request needs.
    if (module == QLatin1String("conn")) {
        if (name == QLatin1String("Ready")) {
            setOnline(true);
            // Not reload(): that empties the window the user is reading.
            retry();
        } else if (name == QLatin1String("State")) {
            setOnline(a.value(QStringLiteral("state")).toString() ==
                      QLatin1String("connected"));
        } else if (name == QLatin1String("Disconnected") ||
                   name == QLatin1String("ConnError") ||
                   name == QLatin1String("AuthError")) {
            setOnline(false);
        }
        return;
    }
    if (module != QLatin1String("message"))
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

// Downloads are keyed and coalesced by url: the event's id is the file module's
// own counter, and one transfer serves every message quoting that url. An upload
// has no url to share yet, so it keys on id, which for one is the message's own
// timestamp.
//
// The state goes through as it arrives. `idle` is the neutral end - held back
// by the autofetch policy, over its size cap, or cancelled - and reads the same
// to the view as a transfer that never ran, which is exactly what it is.
void ChatModel::handleFileUpdate(const QString &name, const QVariantMap &a) {
    if (name != QLatin1String("Update"))
        return;
    const QString direction = a.value(QStringLiteral("direction")).toString();
    QVariantMap x{{QStringLiteral("state"), a.value(QStringLiteral("state"))},
                  {QStringLiteral("direction"), direction},
                  {QStringLiteral("loaded"), a.value(QStringLiteral("loaded"))},
                  {QStringLiteral("total"), a.value(QStringLiteral("total"))},
                  {QStringLiteral("error"), a.value(QStringLiteral("error"))}};

    if (direction == QLatin1String("upload")) {
        const qlonglong ts = a.value(QStringLiteral("id")).toLongLong();
        if (ts == 0)
            return;
        // The url this carries is the public one the send will quote; the row
        // goes on showing the file it was given, thumbnail and all.
        m_upload.insert(ts, x);
        redrawRow(ts);
        return;
    }
    if (direction != QLatin1String("download"))
        return;

    const QString url = a.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        return;
    const QString thumb = a.value(QStringLiteral("thumbpath")).toString();
    x.insert(QStringLiteral("localpath"), a.value(QStringLiteral("localpath")));
    // Kept as a url, not the path it arrived as: the view needs one, and
    // building it by hand loses to a '#' in a path.
    x.insert(QStringLiteral("thumburl"),
             thumb.isEmpty() ? QUrl() : QUrl::fromLocalFile(thumb));
    m_xfer.insert(url, x);
    redrawRowsUsing(url);
}

void ChatModel::redrawRow(qlonglong ts) {
    const int row = indexOfTs(ts);
    if (row < 0)
        return;
    const QModelIndex mi = index(row);
    emit dataChanged(mi, mi, {AttachmentsRole});
}

void ChatModel::redrawRowsUsing(const QString &url) {
    for (int i = 0; i < m_msgs.size(); ++i) {
        const QVariantList atts = attachmentsOf(m_msgs.at(i));
        for (const QVariant &v : atts) {
            if (v.toMap().value(QStringLiteral("url")).toString() != url)
                continue;
            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi, {AttachmentsRole});
            break;
        }
    }
}

// tacky downloads the image (or reads a local source in place), derives the
// thumbnail and reports back through file <Update>. `auto` submits the fetch to
// the autofetch policy and its size cap; our own sends are exempt, since from
// history they refetch the public URL that replaced the local path on upload.
void ChatModel::fetchThumbs(const QVariantMap &msg) {
    if (!m_backend || m_account.isEmpty())
        return;
    const bool incoming = !msg.value(QStringLiteral("is_outgoing")).toBool();
    const QVariantList atts = attachmentsOf(msg);
    for (const QVariant &v : atts) {
        const QVariantMap a = v.toMap();
        if (a.value(QStringLiteral("type")).toString() != QLatin1String("image"))
            continue;
        const QString url = a.value(QStringLiteral("url")).toString();
        if (url.isEmpty())
            continue;
        // Fire-and-forget: progress and the thumbnail arrive as file <Update>.
        // Asked for unconditionally: the file module joins an in-flight
        // download of the same url and serves a finished one from disk, so
        // keeping our own record of what we have asked for would only be a
        // second, staler copy of that.
        m_backend->notify(QStringLiteral("file"), QStringLiteral("download"),
                          QVariantMap{{QStringLiteral("acc"), m_account},
                                      {QStringLiteral("url"), url},
                                      {QStringLiteral("auto"), incoming ? 1 : 0},
                                      {QStringLiteral("thumbmax"), m_thumbMax},
                                      {QStringLiteral("from"),
                                       msg.value(QStringLiteral("from_jid"))}});
    }
}

// The dialog has already asked about overwriting, so a file in the way is one
// the user meant to replace. tacky puts the download where it keeps its files
// and has no notion of anywhere else, so the copy is ours to make.
static QString copyAttachment(const QString &from, const QUrl &to) {
    if (from.isEmpty())
        return QStringLiteral("Could not fetch the file");
    const QString dest = to.isLocalFile() ? to.toLocalFile() : to.toString();
    if (dest.isEmpty())
        return QStringLiteral("Nowhere to save it");
    QFile::remove(dest);
    QFile src(from);
    return src.copy(dest) ? QString() : src.errorString();
}

// No `auto`, so the policy and the size cap don't apply: this is the one the
// user pointed at.
void ChatModel::loadAttachment(qlonglong ts, int idx) {
    const QVariantMap a = attachmentAt(ts, idx);
    const QString url = a.value(QStringLiteral("url")).toString();
    if (url.isEmpty() || !m_backend || m_account.isEmpty())
        return;
    m_backend->notify(QStringLiteral("file"), QStringLiteral("download"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("url"), url},
                                  {QStringLiteral("thumbmax"), m_thumbMax}});
}

// Opening, saving and revealing all want the file on disk first, and differ
// only in what they then do with it.
void ChatModel::openAttachment(qlonglong ts, int idx) {
    resolveAttachment(ts, idx, {PendingAction::Open, {}});
}

void ChatModel::revealAttachment(qlonglong ts, int idx) {
    resolveAttachment(ts, idx, {PendingAction::Folder, {}});
}

void ChatModel::saveAttachment(qlonglong ts, int idx, const QUrl &dest) {
    if (dest.isEmpty()) // the dialog was dismissed
        return;
    resolveAttachment(ts, idx, {PendingAction::Save, dest});
}

void ChatModel::resolveAttachment(qlonglong ts, int idx,
                                  const PendingAction &act) {
    const QVariantMap a = attachmentAt(ts, idx);
    const QString url = a.value(QStringLiteral("url")).toString();
    if (url.isEmpty())
        return;
    // Already on disk (downloaded, or an outgoing file used in place).
    const QString local = a.value(QStringLiteral("localpath")).toString();
    if (!local.isEmpty()) {
        finishAction(act, local);
        return;
    }
    if (!m_backend || m_account.isEmpty()) {
        finishAction(act, {});
        return;
    }
    // The file module answers with the local path, or "" if it could not get
    // one - a failure, not an error reply, so there is no error leg to handle.
    const int tok = m_backend->request(QStringLiteral("file"),
                                       QStringLiteral("download"),
                                       QVariantMap{{QStringLiteral("acc"), m_account},
                                                   {QStringLiteral("url"), url},
                                                   {QStringLiteral("thumbmax"), m_thumbMax}});
    m_pendingAction.insert(tok, act);
}

// An empty `path` is the file module saying it could not get one, which each of
// these has to have a way of showing.
void ChatModel::finishAction(const PendingAction &act, const QString &path) {
    switch (act.kind) {
    case PendingAction::Open:
        emit attachmentResolved(path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path));
        break;
    case PendingAction::Folder:
        emit attachmentFolder(
            path.isEmpty() ? QUrl()
                           : QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        break;
    case PendingAction::Save:
        emit attachmentSaved(act.dest, copyAttachment(path, act.dest));
        break;
    }
}

// An upload is cancelled by the message it belongs to, a download by the url
// every message quoting it waits on.
void ChatModel::cancelAttachment(qlonglong ts, int idx) {
    const QVariantMap a = attachmentAt(ts, idx);
    if (a.isEmpty() || !m_backend || m_account.isEmpty())
        return;
    QVariantMap args{{QStringLiteral("acc"), m_account}};
    if (a.value(QStringLiteral("direction")).toString() == QLatin1String("upload"))
        args.insert(QStringLiteral("id"), ts);
    else
        args.insert(QStringLiteral("url"), a.value(QStringLiteral("url")));
    m_backend->notify(QStringLiteral("file"), QStringLiteral("cancel"), args);
}

// tacky deletes them and says nothing afterwards, so what we remember of the
// transfer goes here too: the row would otherwise go on showing a thumbnail for
// a file that is gone.
void ChatModel::uncacheAttachment(qlonglong ts, int idx) {
    const QVariantMap a = attachmentAt(ts, idx);
    const QString url = a.value(QStringLiteral("url")).toString();
    if (url.isEmpty() || !m_backend || m_account.isEmpty())
        return;
    m_backend->notify(QStringLiteral("file"), QStringLiteral("uncache"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("url"), url}});
    m_xfer.remove(url);
    redrawRowsUsing(url);
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
    if (m_pendingAction.contains(token)) {
        finishAction(m_pendingAction.take(token), data.toString());
        return;
    }
    if (!m_pending.contains(token))
        return;
    const QString role = m_pending.take(token);
    markInflight(role, false);
    clearFailure(role);

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
        // Only on the insert: a patch is a status or an edit, neither of which
        // brings an attachment with it.
        fetchThumbs(msg);
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
    // The upload that put it here is keyed by the id it had then.
    if (m_upload.contains(ts))
        m_upload.insert(newTs, m_upload.take(ts));
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
