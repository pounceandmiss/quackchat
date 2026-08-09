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
    void anchorOnScreenKeepsTheWindow();
    void anchorOffScreenReplacesTheWindow();
    void unresolvedReplyTargetMovesNothing();
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

QTEST_MAIN(TestChatModel)
#include "tst_chatmodel.moc"
