#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "ChatModel.h"
#include "MessageMarkup.h"
#include "TackyBackend.h"

static void feedEvent(ChatModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QVariantList msgs(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

static QVariantList spans(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

// Stands in for the palette's; the markup only ever passes it through.
static const QString kQuote = QStringLiteral("#0a0");

class TestChatModel : public QObject {
    Q_OBJECT
private slots:
    void ordersNewestFirst();
    void insertsIntoMiddle();
    void dedupPatchesInPlace();
    void liveInsertWhenAtTail();
    void liveDropWhenOffTail();
    void filtersAccAndChat();
    void statusFieldUpdate();
    void confirmedTimestampMove();
    void retractedTombstone();
    void cullNewClearsTail();
    void cullOldKeepsTail();
    void loadedSignalReportsAdded();
    void loadingOlderTracksTheOldRequest();
    void markupWrapsSpans();
    void markupNestsOverlappingSpans();
    void markupCountsCodePoints();
    void markupEscapesAndKeepsWhitespace();
    void markupRoleReadsTheContentUnion();
    void markupRoleFollowsTheQuoteColor();
    void replyRolesAreAlwaysStrings();
    void encryptionRolesAreAlwaysStrings();
    void statusCarriesTheFailureReason();
    void plaintextResendClearsTheStamp();
    void partialStatusLeavesTheFailureReason();
    void remoteStatusTracksTheFarEnd();
    void reactionsComeFromTheAggregatedMap();
    void anchorOnScreenKeepsTheWindow();
    void anchorOffScreenReplacesTheWindow();
    void unresolvedReplyTargetMovesNothing();
    void attachmentsRoleReadsTheContentUnion();
    void fileUpdateMergesIntoTheRow();
    void fileUpdateFansOutToEveryRowSharingTheUrl();
    void transferStatesReachTheViewIntact();
    void fileEventsFilterByAcc();
    void imageRowsAskForTheirThumbnails();
    void openAttachmentResolvesThroughTheBackend();
    void catchupGatesLiveInserts();
    void catchupBracketMatching();
    void catchupReconcileRepages();
    void catchupReconcileReloadsEmptyWindow();
};

void TestChatModel::ordersNewestFirst() {
    ChatModel m;
    m.applyBatch(msgs(R"([
        {"timestamp":100,"body":"a"},
        {"timestamp":300,"body":"c"},
        {"timestamp":200,"body":"b"}
    ])"));
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 300LL);
    QCOMPARE(m.data(m.index(1), ChatModel::TimestampRole).toLongLong(), 200LL);
    QCOMPARE(m.data(m.index(2), ChatModel::TimestampRole).toLongLong(), 100LL);
}

void TestChatModel::insertsIntoMiddle() {
    ChatModel m;
    m.applyBatch(msgs(R"([{"timestamp":300},{"timestamp":100}])"));
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    m.applyBatch(msgs(R"([{"timestamp":200}])"));
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(ins.count(), 1);
    QCOMPARE(ins.first().at(1).toInt(), 1); // between 300 and 100
    QCOMPARE(m.data(m.index(1), ChatModel::TimestampRole).toLongLong(), 200LL);
}

void TestChatModel::dedupPatchesInPlace() {
    ChatModel m;
    m.applyBatch(msgs(R"([{"timestamp":300,
        "content":{"type":"text","body":"x"},"server_status":"pending"}])"));
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    m.applyBatch(msgs(R"([{"timestamp":300,
        "content":{"type":"text","body":"y"},"server_status":""}])"));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(ins.count(), 0);
    QCOMPARE(chg.count(), 1);
    QCOMPARE(m.data(m.index(0), ChatModel::BodyRole).toString(), QString("y"));
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());
}

void TestChatModel::liveInsertWhenAtTail() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h"); // reload -> atTail true (no backend, load is a no-op)
    QVERIFY(m.atTail());
    feedEvent(m, R"(["event","message","New",{"acc":"me@h","jid":"a@h",
        "message":{"timestamp":500,"content":{"type":"text","body":"hi"}}}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), ChatModel::BodyRole).toString(), QString("hi"));
}

void TestChatModel::liveDropWhenOffTail() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyGotoSlice(msgs(R"([{"timestamp":100,
        "content":{"type":"text","body":"old"}}])")); // atTail -> false
    QVERIFY(!m.atTail());
    feedEvent(m, R"(["event","message","New",{"acc":"me@h","jid":"a@h",
        "message":{"timestamp":500,"content":{"type":"text","body":"new"}}}])");
    QCOMPARE(m.rowCount(), 1); // dropped by the gate
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 100LL);
}

void TestChatModel::filtersAccAndChat() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    feedEvent(m, R"(["event","message","New",
        {"acc":"other@h","jid":"a@h","message":{"timestamp":1}}])");
    feedEvent(m, R"(["event","message","New",
        {"acc":"me@h","jid":"b@h","message":{"timestamp":2}}])");
    QCOMPARE(m.rowCount(), 0);
}

void TestChatModel::statusFieldUpdate() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"server_status":"pending"}])"));
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","message","Status",
        {"acc":"me@h","jid":"a@h","timestamp":300,"server_status":""}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(chg.count(), 1);
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());
}

void TestChatModel::confirmedTimestampMove() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300},{"timestamp":200},{"timestamp":100}])"));
    // 200 gets a server time of 400 -> should jump to the newest row
    feedEvent(m, R"(["event","message","Confirmed",
        {"acc":"me@h","jid":"a@h","timestamp":200,"newtimestamp":400,
         "server_status":""}])");
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 400LL);
    QCOMPARE(m.data(m.index(1), ChatModel::TimestampRole).toLongLong(), 300LL);
    QCOMPARE(m.data(m.index(2), ChatModel::TimestampRole).toLongLong(), 100LL);
}

void TestChatModel::retractedTombstone() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,
        "content":{"type":"text","body":"oops"}}])"));
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","message","Retracted",
        {"acc":"me@h","jid":"a@h","timestamp":300}])");
    QCOMPARE(m.rowCount(), 1); // row kept for anchoring
    QCOMPARE(chg.count(), 1);
    QVERIFY(m.data(m.index(0), ChatModel::RetractedRole).toBool());
    QCOMPARE(m.data(m.index(0), ChatModel::BodyRole).toString(), QString());
}

void TestChatModel::cullNewClearsTail() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300},{"timestamp":200},{"timestamp":100}])"));
    QVERIFY(m.atTail());
    m.cullNew(1);
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(!m.atTail());
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 200LL);
}

void TestChatModel::cullOldKeepsTail() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300},{"timestamp":200},{"timestamp":100}])"));
    QVERIFY(m.atTail());
    m.cullOld(1);
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(m.atTail());
    QCOMPARE(m.data(m.index(1), ChatModel::TimestampRole).toLongLong(), 200LL);
}

// The view uses loaded(dir, added) to keep filling an under-tall viewport and
// to notice when the archive is exhausted (added == 0). request() allocates
// tokens even on an unstarted backend, so the init load is token 1.
void TestChatModel::loadedSignalReportsAdded() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // issues the initial history as token 1

    QSignalSpy loaded(&m, &ChatModel::loaded);
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QString("init"));
    QCOMPARE(loaded.first().at(1).toInt(), 2);

    m.loadOlder();                 // token 2
    m.handleResult(2, msgs("[]")); // archive dry
    QCOMPARE(loaded.count(), 2);
    QCOMPARE(loaded.at(1).at(0).toString(), QString("old"));
    QCOMPARE(loaded.at(1).at(1).toInt(), 0);
}

// Drives the feed's loading pill. The view keeps no in-flight latch of its own
// and leans on the refusal below instead, since a request that never answers
// would wedge one forever.
void TestChatModel::loadingOlderTracksTheOldRequest() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // issues the initial history as token 1

    QSignalSpy busy(&m, &ChatModel::loadingOlderChanged);
    QVERIFY(m.loadingOlder()); // the empty window is waiting on its first page
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));
    QVERIFY(!m.loadingOlder());
    QCOMPARE(busy.count(), 1);

    m.loadOlder(); // token 2
    QVERIFY(m.loadingOlder());
    m.loadOlder(); // refused while one is out, so token 3 is never issued
    m.handleResult(3, msgs(R"([{"timestamp":50}])"));
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(m.loadingOlder());

    m.handleResult(2, msgs(R"([{"timestamp":50}])"));
    QCOMPARE(m.rowCount(), 3);
    QVERIFY(!m.loadingOlder());
}

// One tag pair per span type, and nothing at all when there is nothing to mark
// up - the empty string is what puts the bubble back on the plain-text path.
void TestChatModel::markupWrapsSpans() {
    QCOMPARE(messageMarkup("plain", {}, kQuote), QString());
    QCOMPARE(messageMarkup("", spans(R"([{"type":"bold","offset":0,"length":1}])"), kQuote),
             QString());
    // A type we don't know draws as text rather than leaking brackets.
    QCOMPARE(messageMarkup("hi", spans(R"([{"type":"sparkle","offset":0,"length":2}])"), kQuote),
             QString());

    QCOMPARE(messageMarkup("hi there",
                           spans(R"([{"type":"bold","offset":0,"length":2}])"), kQuote),
             QString("<b>hi</b> there"));
    QCOMPARE(messageMarkup("a b",
                           spans(R"([{"type":"italic","offset":2,"length":1}])"), kQuote),
             QString("a <i>b</i>"));
    QCOMPARE(messageMarkup("gone",
                           spans(R"([{"type":"overstrike","offset":0,"length":4}])"), kQuote),
             QString("<s>gone</s>"));
    QCOMPARE(messageMarkup("ls -l",
                           spans(R"([{"type":"monospace","offset":0,"length":5}])"), kQuote),
             QString("<span style=\"font-family:monospace\">ls -l</span>"));
    // Colored, not indented: tacky leaves the "> " markers in the body, and a
    // <blockquote> would stack Qt's own indent and block break on top of them.
    QCOMPARE(messageMarkup("> said so",
                           spans(R"([{"type":"quote","offset":0,"length":9}])"), kQuote),
             QString("<span style=\"color:#0a0\">&gt; said so</span>"));

    // A length running past the end is clamped, not dropped.
    QCOMPARE(messageMarkup("hi", spans(R"([{"type":"bold","offset":0,"length":99}])"), kQuote),
             QString("<b>hi</b>"));
}

// XEP-0393 spans overlap without nesting, so a run inside another has to close
// and reopen rather than emit crossed tags. A block style stays whole across an
// inline one inside it.
void TestChatModel::markupNestsOverlappingSpans() {
    QCOMPARE(messageMarkup("abcd", spans(R"([
        {"type":"bold","offset":0,"length":3},
        {"type":"italic","offset":2,"length":2}])"), kQuote),
             QString("<b>ab<i>c</i></b><i>d</i>"));

    // The quote is one run, not one per styled stretch inside it.
    QCOMPARE(messageMarkup("a b c", spans(R"([
        {"type":"quote","offset":0,"length":5},
        {"type":"bold","offset":2,"length":1}])"), kQuote),
             QString("<span style=\"color:#0a0\">a <b>b</b> c</span>"));
}

// tacky counts offsets in code points; QString indexes UTF-16, so anything past
// an emoji lands a place early if the two are confused.
void TestChatModel::markupCountsCodePoints() {
    QCOMPARE(messageMarkup(QString::fromUtf8("😀 hi"),
                           spans(R"([{"type":"bold","offset":2,"length":2}])"), kQuote),
             QString::fromUtf8("😀 <b>hi</b>"));
}

void TestChatModel::markupEscapesAndKeepsWhitespace() {
    QCOMPARE(messageMarkup("a<b>&c", spans(R"([{"type":"bold","offset":0,"length":1}])"), kQuote),
             QString("<b>a</b>&lt;b&gt;&amp;c"));
    // Newlines and runs of spaces survive HTML's whitespace collapsing...
    QCOMPARE(messageMarkup("a\n  b", spans(R"([{"type":"bold","offset":0,"length":1}])"), kQuote),
             QString("<b>a</b><br> &nbsp;b"));
    // ...and inside <pre> they are already literal.
    QCOMPARE(messageMarkup("a\n  b",
                           spans(R"([{"type":"preformatted","offset":0,"length":5}])"), kQuote),
             QString("<pre>a\n  b</pre>"));
}

// The spans index into whatever string the body role returned, so the role has
// to read them out of the same content variant - caption for media, not body.
void TestChatModel::markupRoleReadsTheContentUnion() {
    ChatModel m;
    m.applyBatch(msgs(R"([
        {"timestamp":300,"content":{"type":"media","caption":"a shot",
            "formatting":[{"type":"bold","offset":2,"length":4}]}},
        {"timestamp":200,"content":{"type":"text","body":"hi",
            "formatting":[{"type":"italic","offset":0,"length":2}]}},
        {"timestamp":100,"content":{"type":"text","body":"bare"}}
    ])"));
    QCOMPARE(m.data(m.index(0), ChatModel::MarkupRole).toString(),
             QString("a <b>shot</b>"));
    QCOMPARE(m.data(m.index(1), ChatModel::MarkupRole).toString(),
             QString("<i>hi</i>"));
    QCOMPARE(m.data(m.index(2), ChatModel::MarkupRole).toString(), QString());

    // A retraction takes the body with it, markup included.
    m.applyRetracted(200);
    QCOMPARE(m.data(m.index(1), ChatModel::MarkupRole).toString(), QString());
}

// Ctrl+T swaps the palette under an open chat, and the color is baked into the
// markup, so the rows carrying one have to be told to redraw.
void TestChatModel::markupRoleFollowsTheQuoteColor() {
    ChatModel m;
    m.setQuoteColor("#111111");
    m.applyBatch(msgs(R"([{"timestamp":100,"content":{"type":"text","body":"> hi",
        "formatting":[{"type":"quote","offset":0,"length":4}]}}])"));
    QVERIFY(m.data(m.index(0), ChatModel::MarkupRole).toString().contains("#111111"));

    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    m.setQuoteColor("#222222");
    QCOMPARE(chg.count(), 1);
    QCOMPARE(chg.first().at(2).value<QList<int>>(), QList<int>{ChatModel::MarkupRole});
    QVERIFY(m.data(m.index(0), ChatModel::MarkupRole).toString().contains("#222222"));
}

// The backend owns the set: react only asks, and the answer arrives as a
// <Reactions> event carrying the whole aggregated map.
void TestChatModel::reactionsComeFromTheAggregatedMap() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300}])"));
    QVERIFY(m.data(m.index(0), ChatModel::ReactionsRole).toMap().isEmpty());

    // Asking does not change the row; only the event that follows does.
    m.react(300, "👍");
    QVERIFY(m.data(m.index(0), ChatModel::ReactionsRole).toMap().isEmpty());

    feedEvent(m, R"(["event","message","Reactions",{"acc":"me@h","jid":"a@h",
        "timestamp":300,"reactions":{"👍":{"reactors":["me@h","b@h"],"mine":true},
                                     "🙏":{"reactors":["b@h"],"mine":false}}}])");
    const QVariantMap r = m.data(m.index(0), ChatModel::ReactionsRole).toMap();
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.value("👍").toMap().value("reactors").toList().size(), 2);
    QVERIFY(r.value("👍").toMap().value("mine").toBool());
    QVERIFY(!r.value("🙏").toMap().value("mine").toBool());

    // The map is replaced wholesale, never merged.
    feedEvent(m, R"(["event","message","Reactions",
        {"acc":"me@h","jid":"a@h","timestamp":300,"reactions":{}}])");
    QVERIFY(m.data(m.index(0), ChatModel::ReactionsRole).toMap().isEmpty());
}

// The two hops are separate fields and separate roles; <Status> can move
// either without touching the other.
void TestChatModel::remoteStatusTracksTheFarEnd() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"server_status":"pending",
                           "remote_status":"none"}])"));
    QCOMPARE(m.data(m.index(0), ChatModel::RemoteStatusRole).toString(), QString("none"));

    feedEvent(m, R"(["event","message","Status",
        {"acc":"me@h","jid":"a@h","timestamp":300,"server_status":""}])");
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());
    QCOMPARE(m.data(m.index(0), ChatModel::RemoteStatusRole).toString(), QString("none"));

    feedEvent(m, R"(["event","message","Status",
        {"acc":"me@h","jid":"a@h","timestamp":300,"remote_status":"read"}])");
    QCOMPARE(m.data(m.index(0), ChatModel::RemoteStatusRole).toString(), QString("read"));
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());

    // Same undefined trap as the reply roles: a row without the key must read
    // as an empty string, not as a missing variant.
    m.applyBatch(msgs(R"([{"timestamp":100}])"));
    QCOMPARE(m.data(m.index(1), ChatModel::RemoteStatusRole).typeId(), QMetaType::QString);
}

// An ordinary message carries no reply keys at all. Handing QML the missing
// variant lets it through as undefined, which a string property in the delegate
// renders as the word "undefined" - so every bubble grew a quote.
void TestChatModel::replyRolesAreAlwaysStrings() {
    ChatModel m;
    m.applyBatch(msgs(R"([{"timestamp":100,"content":{"type":"text","body":"hi"}}])"));
    const QVariant body = m.data(m.index(0), ChatModel::ReplyBodyRole);
    const QVariant author = m.data(m.index(0), ChatModel::ReplyAuthorRole);
    QCOMPARE(body.typeId(), QMetaType::QString);
    QCOMPARE(author.typeId(), QMetaType::QString);
    QVERIFY(body.toString().isEmpty());
    QVERIFY(author.toString().isEmpty());
}

// Same undefined trap as the reply roles, and the same fix: a cleartext row
// carries neither key, and a bubble reading them must see empty strings.
void TestChatModel::encryptionRolesAreAlwaysStrings() {
    ChatModel m;
    m.applyBatch(msgs(R"([{"timestamp":100,"content":{"type":"text","body":"hi"}}])"));
    const QVariant enc = m.data(m.index(0), ChatModel::EncryptionRole);
    const QVariant why = m.data(m.index(0), ChatModel::FailReasonRole);
    QCOMPARE(enc.typeId(), QMetaType::QString);
    QCOMPARE(why.typeId(), QMetaType::QString);
    QVERIFY(enc.toString().isEmpty());
    QVERIFY(why.toString().isEmpty());
}

// An encryption that never came off fails the row outright rather than going
// out in clear, and says so in fail_reason - which is what offers the user a
// plaintext resend instead of a plain retry.
void TestChatModel::statusCarriesTheFailureReason() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"is_outgoing":true,
        "encryption":"omemo","server_status":"pending"}])"));
    feedEvent(m, R"(["event","message","Status",{"acc":"me@h","jid":"a@h",
        "timestamp":300,"server_status":"failed","fail_reason":"encrypt"}])");
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString("failed"));
    QCOMPARE(m.data(m.index(0), ChatModel::FailReasonRole).toString(), QString("encrypt"));
    QCOMPARE(m.data(m.index(0), ChatModel::EncryptionRole).toString(), QString("omemo"));
}

// A plaintext resend rewrites the row's stamp, and the padlock has to come off
// with it - a message drawn as encrypted that went out in clear is the worst
// thing this feature can do. The backend reports the new stamp on <Status>.
void TestChatModel::plaintextResendClearsTheStamp() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"is_outgoing":true,
        "encryption":"omemo","server_status":"failed","fail_reason":"encrypt"}])"));
    feedEvent(m, R"(["event","message","Status",{"acc":"me@h","jid":"a@h",
        "timestamp":300,"server_status":"pending","fail_reason":"","encryption":""}])");
    QCOMPARE(m.data(m.index(0), ChatModel::EncryptionRole).toString(), QString());
    QCOMPARE(m.data(m.index(0), ChatModel::FailReasonRole).toString(), QString());
}

// <Status> carries only what changed, and an upload failure names no reason at
// all - so a stale fail_reason outlives the failure it described. Anything
// gating on it has to read the status alongside it.
void TestChatModel::partialStatusLeavesTheFailureReason() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"is_outgoing":true,
        "server_status":"failed","fail_reason":"encrypt"}])"));
    feedEvent(m, R"(["event","message","Status",
        {"acc":"me@h","jid":"a@h","timestamp":300,"server_status":""}])");
    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());
    QCOMPARE(m.data(m.index(0), ChatModel::FailReasonRole).toString(), QString("encrypt"));
}

// goto_result's anchor decides the work: one already displayed only needs
// scrolling to, so the window - and the at-tail flag with it - stays put.
void TestChatModel::anchorOnScreenKeepsTheWindow() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");                // initial history is token 1
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));
    QVERIFY(m.atTail());

    QSignalSpy anchored(&m, &ChatModel::anchored);
    m.gotoTimestamp(100);            // token 2
    m.handleResult(2, QJsonDocument::fromJson(R"({
        "anchor":100,"messages":[{"timestamp":100}]})").object().toVariantMap());
    QCOMPARE(anchored.count(), 1);
    QCOMPARE(anchored.first().at(0).toLongLong(), 100LL);
    QCOMPARE(m.rowCount(), 2);       // untouched
    QVERIFY(m.atTail());
}

void TestChatModel::anchorOffScreenReplacesTheWindow() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.handleResult(1, msgs(R"([{"timestamp":900}])"));
    QVERIFY(m.atTail());

    QSignalSpy anchored(&m, &ChatModel::anchored);
    m.gotoTimestamp(20);
    m.handleResult(2, QJsonDocument::fromJson(R"({
        "anchor":20,"messages":[{"timestamp":10},{"timestamp":20}]})")
                          .object().toVariantMap());
    QCOMPARE(anchored.first().at(0).toLongLong(), 20LL);
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 20LL);
    QVERIFY(!m.atTail());            // the slice need not reach the tail
}

// An uncached target answers an empty anchor. Clearing the window on that
// would strand the user on a blank feed for a tap that resolved to nothing.
void TestChatModel::unresolvedReplyTargetMovesNothing() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.handleResult(1, msgs(R"([{"timestamp":100,"reply_id":"x","reply_to":"b@h"},
                               {"timestamp":50}])"));

    QSignalSpy anchored(&m, &ChatModel::anchored);
    m.gotoReplyTarget(100);          // token 2
    m.handleResult(2, QJsonDocument::fromJson(R"({"anchor":"","messages":[]})")
                          .object().toVariantMap());
    QCOMPARE(anchored.count(), 1);
    QCOMPARE(anchored.first().at(0).toLongLong(), 0LL);
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(m.atTail());

    // A row that is not a reply has nothing to jump to, and never asks.
    anchored.clear();
    m.gotoReplyTarget(50);
    QCOMPARE(anchored.count(), 1);
    QCOMPARE(anchored.first().at(0).toLongLong(), 0LL);
}

void TestChatModel::catchupGatesLiveInserts() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    QVERIFY(m.atTail());
    feedEvent(m, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":"a@h"}])");
    QVERIFY(m.catchupBusy());
    feedEvent(m, R"(["event","message","New",{"acc":"me@h","jid":"a@h",
        "message":{"timestamp":500,"content":{"type":"text","body":"mid-sync"}}}])");
    QCOMPARE(m.rowCount(), 0); // dropped while the bracket is open
    feedEvent(m, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"a@h","count":1}])");
    QVERIFY(!m.catchupBusy());
    feedEvent(m, R"(["event","message","New",{"acc":"me@h","jid":"a@h",
        "message":{"timestamp":600,"content":{"type":"text","body":"live"}}}])");
    QCOMPARE(m.rowCount(), 1);
}

// The account-wide bracket (empty jid) covers 1:1 chats only; a room listens
// solely to its own bracket.
void TestChatModel::catchupBracketMatching() {
    ChatModel one2one;
    one2one.setAccount("me@h");
    one2one.setChat("a@h");
    feedEvent(one2one, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":""}])");
    QVERIFY(one2one.catchupBusy());
    feedEvent(one2one, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"","count":0}])");
    QVERIFY(!one2one.catchupBusy());
    feedEvent(one2one, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":"other@h"}])");
    QVERIFY(!one2one.catchupBusy()); // someone else's room sync

    ChatModel muc;
    muc.setAccount("me@h");
    muc.setChat("room@muc.h");
    muc.setGroupchat(true);
    feedEvent(muc, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":""}])");
    QVERIFY(!muc.catchupBusy());
    feedEvent(muc, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":"room@muc.h"}])");
    QVERIFY(muc.catchupBusy());
    feedEvent(muc, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"","count":0}])");
    QVERIFY(muc.catchupBusy()); // foreign close leaves the bracket open
    feedEvent(muc, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"room@muc.h","count":0}])");
    QVERIFY(!muc.catchupBusy());
}

// After a catchup, a window whose newest lags the pushed tail issues an
// `after` page. A page that stops short of the tail is discarded (there is a
// hole in between); one that reaches it is applied.
void TestChatModel::catchupReconcileRepages() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // init request = token 1
    m.handleResult(1, msgs(R"([{"timestamp":200},{"timestamp":100}])"));
    QVERIFY(m.atTail());
    feedEvent(m, R"(["event","message","Tail",
        {"acc":"me@h","jid":"a@h","timestamp":300}])");

    QSignalSpy loaded(&m, &ChatModel::loaded);
    feedEvent(m, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":"a@h"}])");
    feedEvent(m, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"a@h","count":2}])"); // newest 200 != tail 300 -> token 2
    m.handleResult(2, msgs(R"([{"timestamp":250}])")); // stops short of 300
    QCOMPARE(m.rowCount(), 2); // discarded
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QString("catchup"));
    QCOMPARE(loaded.first().at(1).toInt(), 0);

    feedEvent(m, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":"a@h"}])");
    feedEvent(m, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"a@h","count":2}])"); // -> token 3
    m.handleResult(3, msgs(R"([{"timestamp":250},{"timestamp":300}])"));
    QCOMPARE(m.rowCount(), 4); // reaches the tail -> applied
    QVERIFY(m.atTail());
}

// An empty window reconciles by re-running the initial load; the connect-time
// account sync is what fills a chat whose first page came back empty.
void TestChatModel::catchupReconcileReloadsEmptyWindow() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");              // init request = token 1
    m.handleResult(1, msgs("[]"));
    QCOMPARE(m.rowCount(), 0);
    feedEvent(m, R"(["event","message","CatchupStarted",
        {"acc":"me@h","jid":""}])");
    feedEvent(m, R"(["event","message","CatchupDone",
        {"acc":"me@h","jid":"","count":3}])"); // -> fresh init, token 2

    QSignalSpy loaded(&m, &ChatModel::loaded);
    m.handleResult(2, msgs(R"([{"timestamp":100},{"timestamp":200}])"));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(loaded.first().at(0).toString(), QString("init"));
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(m.atTail());
}

static const char *kMediaRow = R"([{"timestamp":100,"is_outgoing":false,
    "from_jid":"her@h","content":{"type":"media","caption":"look",
    "attachments":[{"url":"https://h/a.png","type":"image","name":"a.png",
                    "size":1234,"mime":"image/png"}]}}])";

static QVariantMap att0(const ChatModel &m, int row = 0) {
    return m.data(m.index(row), ChatModel::AttachmentsRole).toList().at(0).toMap();
}

void TestChatModel::attachmentsRoleReadsTheContentUnion() {
    ChatModel m;
    m.applyBatch(msgs(kMediaRow));
    m.applyBatch(msgs(R"([{"timestamp":50,"content":{"type":"text","body":"hi"}}])"));

    const QVariantMap a = att0(m);
    QCOMPARE(a.value("url").toString(), QString("https://h/a.png"));
    QCOMPARE(a.value("type").toString(), QString("image"));
    QCOMPARE(a.value("name").toString(), QString("a.png"));
    // Nothing has happened to it yet, but every key the delegate binds to is
    // present: a missing one would reach QML as undefined.
    QCOMPARE(a.value("state").toString(), QString());
    QVERIFY(a.value("thumburl").toUrl().isEmpty());
    QCOMPARE(a.value("total").toInt(), 0);

    // A text row is an empty list, not an invalid variant.
    const QVariant text = m.data(m.index(1), ChatModel::AttachmentsRole);
    QCOMPARE(text.typeId(), QMetaType::QVariantList);
    QVERIFY(text.toList().isEmpty());

    // A tombstone keeps no attachments either, same as it keeps no body.
    m.applyRetracted(100);
    QVERIFY(m.data(m.index(0), ChatModel::AttachmentsRole).toList().isEmpty());
}

void TestChatModel::fileUpdateMergesIntoTheRow() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(kMediaRow));

    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":7,
        "direction":"download","state":"done","loaded":1234,"total":1234,
        "url":"https://h/a.png","localpath":"/data/a.png",
        "thumbpath":"/cache/a_320.png","error":""}])");

    const QVariantMap a = att0(m);
    QCOMPARE(a.value("state").toString(), QString("done"));
    // The thumbnail reaches the view as a url, which the path it arrived as
    // cannot safely be turned into up there.
    QCOMPARE(a.value("thumburl").toUrl(), QUrl("file:///cache/a_320.png"));
    QCOMPARE(a.value("localpath").toString(), QString("/data/a.png"));
    QCOMPARE(a.value("total").toInt(), 1234);
    // What the message said is still there underneath.
    QCOMPARE(a.value("name").toString(), QString("a.png"));

    QCOMPARE(chg.count(), 1);
    QCOMPARE(chg.first().at(2).value<QList<int>>(),
             QList<int>{ChatModel::AttachmentsRole});

    // An upload update is not ours to read: it keys on the message id, and
    // matching it by url would credit the wrong row.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":100,
        "direction":"upload","state":"active","loaded":10,"total":99,
        "url":"","localpath":"","thumbpath":"","error":""}])");
    QCOMPARE(att0(m).value("state").toString(), QString("done"));
}

// One download serves every message quoting the URL, so all of them redraw.
void TestChatModel::fileUpdateFansOutToEveryRowSharingTheUrl() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(R"([
        {"timestamp":200,"content":{"type":"media","attachments":[
            {"url":"https://h/a.png","type":"image","name":"a.png"}]}},
        {"timestamp":100,"content":{"type":"media","attachments":[
            {"url":"https://h/a.png","type":"image","name":"a.png"}]}},
        {"timestamp":50,"content":{"type":"media","attachments":[
            {"url":"https://h/b.png","type":"image","name":"b.png"}]}}
    ])"));

    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","url":"https://h/a.png","thumbpath":"/cache/a.png"}])");

    QCOMPARE(chg.count(), 2);
    QCOMPARE(att0(m, 0).value("thumburl").toUrl(), QUrl("file:///cache/a.png"));
    QCOMPARE(att0(m, 1).value("thumburl").toUrl(), QUrl("file:///cache/a.png"));
    QVERIFY(att0(m, 2).value("thumburl").toUrl().isEmpty());
}

// The policy holding an image back is a decision, not an error: the view shows
// a tap-to-load chip, and must not be handed a failure to complain about.
// The states reach the view as tacky sent them. `idle` - held back, capped or
// cancelled - is the neutral end and carries no error to show; only a real
// failure does, and that is what the chip offers a retry on.
void TestChatModel::transferStatesReachTheViewIntact() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(kMediaRow));

    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"idle","url":"https://h/a.png","error":""}])");
    QCOMPARE(att0(m).value("state").toString(), QString("idle"));
    QVERIFY(att0(m).value("error").toString().isEmpty());
    QVERIFY(att0(m).value("thumburl").toUrl().isEmpty());

    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"failed","url":"https://h/a.png","error":"http error"}])");
    QCOMPARE(att0(m).value("state").toString(), QString("failed"));
    QCOMPARE(att0(m).value("error").toString(), QString("http error"));
}

void TestChatModel::fileEventsFilterByAcc() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(kMediaRow));

    feedEvent(m, R"(["event","file","Update",{"acc":"other@h","direction":"download",
        "state":"done","url":"https://h/a.png","thumbpath":"/cache/a.png"}])");
    QVERIFY(att0(m).value("thumburl").toUrl().isEmpty());
}

// The fetch is what makes a thumbnail exist, and its arguments are what submit
// it to the autofetch policy - so the whole of the row's inline image hangs on
// getting this call right.
void TestChatModel::imageRowsAskForTheirThumbnails() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // the initial history request

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.applyBatch(msgs(R"([
        {"timestamp":300,"is_outgoing":false,"from_jid":"her@h",
         "content":{"type":"media","attachments":[
            {"url":"https://h/a.png","type":"image","name":"a.png"}]}},
        {"timestamp":200,"is_outgoing":false,"from_jid":"her@h",
         "content":{"type":"media","attachments":[
            {"url":"https://h/a.png","type":"image","name":"a.png"}]}},
        {"timestamp":150,"is_outgoing":false,"from_jid":"her@h",
         "content":{"type":"media","attachments":[
            {"url":"https://h/doc.pdf","type":"file","name":"doc.pdf"}]}},
        {"timestamp":100,"is_outgoing":true,"from_jid":"me@h",
         "content":{"type":"media","attachments":[
            {"url":"/home/me/c.png","type":"image","name":"c.png"}]}}
    ])"));

    QVariantList downloads;
    for (const QList<QVariant> &call : sent)
        if (call.at(0).toString() == "file" && call.at(1).toString() == "download")
            downloads.append(call.at(2));
    // One per image row, the plain file not at all: only an image has a
    // thumbnail to derive. The two rows sharing a url both ask, and the file
    // module joins them into the one transfer.
    QCOMPARE(downloads.size(), 3);

    const QVariantMap first = downloads.at(0).toMap();
    QCOMPARE(first.value("url").toString(), QString("https://h/a.png"));
    QCOMPARE(first.value("acc").toString(), QString("me@h"));
    QCOMPARE(first.value("from").toString(), QString("her@h"));
    QCOMPARE(first.value("auto").toInt(), 1);
    QCOMPARE(downloads.at(1).toMap().value("url").toString(), QString("https://h/a.png"));
    // Our own send is exempt from the policy.
    QCOMPARE(downloads.at(2).toMap().value("auto").toInt(), 0);

    // Tapping a held-back image asks again, this time ungated.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"idle","url":"https://h/a.png","error":""}])");
    sent.clear();
    m.loadAttachment(300, 0);
    QCOMPARE(sent.count(), 1);
    QVERIFY(!sent.first().at(2).toMap().contains("auto"));
}

void TestChatModel::openAttachmentResolvesThroughTheBackend() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // token 1
    m.applyBatch(msgs(kMediaRow));

    QSignalSpy opened(&m, &ChatModel::attachmentResolved);
    m.openAttachment(100, 0); // token 2
    QCOMPARE(opened.count(), 0);
    m.handleResult(2, QVariant(QString("/data/a.png")));
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().at(0).toUrl(), QUrl("file:///data/a.png"));

    // A download that could not deliver answers with an empty path rather than
    // an error, so the view is still told the tap went nowhere.
    m.openAttachment(100, 0); // token 3
    m.handleResult(3, QVariant(QString()));
    QCOMPARE(opened.count(), 1);
    QVERIFY(opened.takeFirst().at(0).toUrl().isEmpty());

    // Once it is on disk there is nothing to ask for.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","url":"https://h/a.png","localpath":"/data/a.png"}])");
    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.openAttachment(100, 0);
    QCOMPARE(sent.count(), 0);
    QCOMPARE(opened.count(), 1);
    QCOMPARE(opened.takeFirst().at(0).toUrl(), QUrl("file:///data/a.png"));

    // A missing index or a missing row is not a tap on anything.
    m.openAttachment(100, 4);
    m.openAttachment(999, 0);
    QCOMPARE(opened.count(), 0);
}

QTEST_MAIN(TestChatModel)
#include "tst_chatmodel.moc"
