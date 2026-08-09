#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "ChatModel.h"
#include "TackyBackend.h"

static void feedEvent(ChatModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QVariantList msgs(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

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
