#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "ChatModel.h"
#include "MessageMarkup.h"
#include "MessageXml.h"
#include "PickedFile.h"
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

// The timestamps handed to `message markOwnRead`, in order.
static QList<qlonglong> marks(const QSignalSpy &spy) {
    QList<qlonglong> out;
    for (const QList<QVariant> &call : spy)
        if (call.at(1).toString() == QLatin1String("markOwnRead"))
            out << call.at(2).toMap().value("timestamp").toLongLong();
    return out;
}

// How many `message history` calls a spy on TackyBackend::sent saw.
static int historyCalls(const QSignalSpy &spy) {
    int n = 0;
    for (const QList<QVariant> &call : spy)
        if (call.at(1).toString() == QLatin1String("history"))
            ++n;
    return n;
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
    void editedMarksTheRow();
    void editAndRetractOnlyAsk();
    void cullNewClearsTail();
    void cullOldKeepsTail();
    void loadedSignalReportsAdded();
    void loadingOlderTracksTheOldRequest();
    void resetToBottomUnwedgesALostInitialLoad();
    void cancelOnlyTellsTheBackendAboutLiveRequests();
    void anErroredRequestDoesNotWedgeTheFeed();
    void aFailedPageSaysSoAndStopsTheAutomaticFill();
    void retryAsksAgainForWhatFailed();
    void comingBackOnlineRetriesOnlyWhatFailed();
    void connStateDrivesTheOnlineFlag();
    void markReadAdvancesTheWatermarkOnlyForwards();
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
    void gotoOutrunsTheOpeningPage();
    void searchMarkJoinsTheStylingSpans();
    void attachmentsRoleReadsTheContentUnion();
    void fileUpdateMergesIntoTheRow();
    void fileUpdateFansOutToEveryRowSharingTheUrl();
    void transferStatesReachTheViewIntact();
    void fileEventsFilterByAcc();
    void anAttachmentWithNoUrlYetKeysOnItsPath();
    void imageRowsAskForTheirThumbnails();
    void thumbnailSizeFollowsTheView();
    void openAttachmentResolvesThroughTheBackend();
    void uploadProgressReachesTheRowItBelongsTo();
    void sendFileHandsTackyThePath();
    void aPickedDocumentIsNamedAfterItself();
    void anUploadedShareEndsUpSent();
    void retryUploadNamesTheRow();
    void cancelUsesTheHandleTheTransferHas();
    void uncacheForgetsTheFileAndItsThumbnail();
    void savingCopiesTheFileWhereItWasAsked();
    void revealAnswersTheFolderTheFileIsIn();
    void catchupGatesLiveInserts();
    void catchupBracketMatching();
    void catchupReconcileRepages();
    void catchupReconcileReloadsEmptyWindow();
    void rawXmlIsAskedOfTheStore();
    void xmlIsLaidOutOneElementPerLine();
    void unparseableXmlIsShownAsItIs();
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
    m.applyBatch(msgs(R"([{"timestamp":300,"encryption":"omemo",
        "reply_body":"the question","reactions":{"👍":{"reactors":["b@h"],"mine":false}},
        "content":{"type":"text","body":"oops"}}])"));
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","message","Retracted",
        {"acc":"me@h","jid":"a@h","timestamp":300}])");
    QCOMPARE(m.rowCount(), 1); // row kept for anchoring
    QCOMPARE(chg.count(), 1);
    QVERIFY(m.data(m.index(0), ChatModel::RetractedRole).toBool());
    QCOMPARE(m.data(m.index(0), ChatModel::BodyRole).toString(), QString());
    // Only the content goes. Everything else the message carried is still on
    // the row - the same row refetched from tacky's store keeps them too - so
    // the tombstone is drawn by not drawing them, not by clearing them here.
    QCOMPARE(m.data(m.index(0), ChatModel::EncryptionRole).toString(),
             QString("omemo"));
    QCOMPARE(m.data(m.index(0), ChatModel::ReplyBodyRole).toString(),
             QString("the question"));
    QVERIFY(!m.data(m.index(0), ChatModel::ReactionsRole).toMap().isEmpty());
}

// The flag rides in on the row, so it works the same on a page of history as
// on the correction that arrives while the chat is open.
void TestChatModel::editedMarksTheRow() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"edited":true,
                           "content":{"type":"text","body":"fixed"}},
                          {"timestamp":200,
                           "content":{"type":"text","body":"as sent"}}])"));
    QVERIFY(m.data(m.index(0), ChatModel::EditedRole).toBool());
    QVERIFY(!m.data(m.index(1), ChatModel::EditedRole).toBool());

    // An <Edited> re-sends the whole row, so the body and the flag land at once.
    feedEvent(m, R"(["event","message","Edited",
        {"acc":"me@h","jid":"a@h","message":{"timestamp":200,"edited":true,
         "content":{"type":"text","body":"corrected"}}}])");
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(1), ChatModel::BodyRole).toString(),
             QString("corrected"));
    QVERIFY(m.data(m.index(1), ChatModel::EditedRole).toBool());
}

// Both are asks: tacky swaps its own store and answers with an event, so
// nothing about the row changes at the point of asking.
void TestChatModel::editAndRetractOnlyAsk() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":300,"is_outgoing":true,
        "content":{"type":"text","body":"teh cat"}}])"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.edit(300, "the cat");
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("message"));
    QCOMPARE(sent.first().at(1).toString(), QString("edit"));
    QVariantMap args = sent.first().at(2).toMap();
    QCOMPARE(args.value("acc").toString(), QString("me@h"));
    QCOMPARE(args.value("chat").toString(), QString("a@h"));
    QCOMPARE(args.value("timestamp").toLongLong(), 300LL);
    QCOMPARE(args.value("body").toString(), QString("the cat"));
    // Still the old words until the <Edited> comes back.
    QCOMPARE(m.data(m.index(0), ChatModel::BodyRole).toString(),
             QString("teh cat"));
    QVERIFY(!m.data(m.index(0), ChatModel::EditedRole).toBool());

    sent.clear();
    m.retract(300);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(1).toString(), QString("retract"));
    args = sent.first().at(2).toMap();
    QCOMPARE(args.value("timestamp").toLongLong(), 300LL);
    QVERIFY(!args.contains("body"));
    QVERIFY(!m.data(m.index(0), ChatModel::RetractedRole).toBool());

    // An empty correction is not one; tacky drops it on the far side anyway.
    sent.clear();
    m.edit(300, "");
    QCOMPARE(sent.count(), 0);
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

// tacky's notify gate is the read watermark: without this the chat on screen
// still alerts. markOwnRead is forward-only, and the view calls markRead on
// every insert, so an unchanged watermark must not become a frame.
void TestChatModel::markReadAdvancesTheWatermarkOnlyForwards() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([{"timestamp":100},{"timestamp":300}])"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.markRead();
    QCOMPARE(marks(sent), QList<qlonglong>{300});

    sent.clear();
    m.markRead(); // nothing newer arrived
    QCOMPARE(marks(sent), QList<qlonglong>{});

    m.applyBatch(msgs(R"([{"timestamp":400}])"));
    sent.clear();
    m.markRead();
    QCOMPARE(marks(sent), QList<qlonglong>{400});
}

// An error reply is as final as a lost one. Without clearing the direction the
// pill stays lit and issueHistory refuses every retry, exactly as a dropped
// reply used to do.
void TestChatModel::anErroredRequestDoesNotWedgeTheFeed() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // init is token 1
    QVERIFY(m.loadingOlder());

    // Through the signal, not the handler: the bug was never connecting it.
    emit backend.error(1, "no such column: m.mentions_me");
    QVERIFY(!m.loadingOlder());

    m.loadInitial(); // token 2: refused if init were still in flight
    m.handleResult(2, msgs(R"([{"timestamp":100}])"));
    QCOMPARE(m.rowCount(), 1);
}

// An empty page means the archive is dry and the view stops asking; a failed
// one means nothing of the sort, so it must not be reported the same way.
void TestChatModel::aFailedPageSaysSoAndStopsTheAutomaticFill() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));

    QSignalSpy loaded(&m, &ChatModel::loaded);
    m.loadOlder(); // token 2
    QVERIFY(m.loadingOlder());
    m.handleError(2, "Couldn't reach the message archive");

    QVERIFY(!m.loadingOlder());
    QCOMPARE(m.loadError(), QString("Couldn't reach the message archive"));
    QVERIFY2(loaded.isEmpty(),
             "loaded(dir, 0) tells the view the archive ran dry, and it "
             "latches on it");

    // The view's fill re-fires on every scroll; none of those may go out.
    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.loadOlder();
    m.loadOlder();
    QCOMPARE(historyCalls(sent), 0);
}

void TestChatModel::retryAsksAgainForWhatFailed() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));
    m.loadOlder(); // token 2
    m.handleError(2, "Couldn't reach the message archive");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.retry();
    QCOMPARE(historyCalls(sent), 1);
    QVERIFY(m.loadingOlder());
    QVERIFY(m.loadError().isEmpty()); // asking again is what clears it

    m.handleResult(3, msgs(R"([{"timestamp":50}])"));
    QCOMPARE(m.rowCount(), 3);
}

// A stream coming back is the one event that can fix a page the archive could
// not answer - without reload()'s reset of the window being read.
void TestChatModel::comingBackOnlineRetriesOnlyWhatFailed() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.handleResult(1, msgs(R"([{"timestamp":100},{"timestamp":200}])"));

    QSignalSpy quiet(&backend, &TackyBackend::sent);
    feedEvent(m, R"(["event","conn","State",{"acc":"me@h","state":"connected"}])");
    QCOMPARE(historyCalls(quiet), 0); // nothing failed, nothing to redo

    m.loadOlder(); // token 2
    m.handleError(2, "Couldn't reach the message archive");
    QCOMPARE(m.rowCount(), 2);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    feedEvent(m, R"(["event","conn","State",{"acc":"me@h","state":"connected"}])");
    QCOMPARE(historyCalls(sent), 1);
    QCOMPARE(m.rowCount(), 2); // the window it was reading is still there
    QVERIFY(m.loadError().isEmpty());
}

// A page waiting on a connection is buffered by tacky, not lost. The view has
// to tell that apart from one actually in progress, so it does not spin.
void TestChatModel::connStateDrivesTheOnlineFlag() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    QVERIFY(!m.online());

    feedEvent(m, R"(["event","conn","State",{"acc":"me@h","state":"connecting"}])");
    QVERIFY(!m.online());
    feedEvent(m, R"(["event","conn","State",{"acc":"me@h","state":"connected"}])");
    QVERIFY(m.online());
    feedEvent(m, R"(["event","conn","Disconnected",{"acc":"me@h"}])");
    QVERIFY(!m.online());

    // Another account's stream says nothing about this one.
    feedEvent(m, R"(["event","conn","State",{"acc":"other@h","state":"connected"}])");
    QVERIFY(!m.online());
}

// Switching chats cancels every direction, but only one is usually out. The
// rest have nothing for the backend to cancel, so they cost no frame.
void TestChatModel::cancelOnlyTellsTheBackendAboutLiveRequests() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // init is the only thing in flight

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.resetToBottom();

    int cancels = 0;
    for (const QList<QVariant> &call : sent)
        if (call.at(1).toString() == QLatin1String("cancel"))
            ++cancels;
    QCOMPARE(cancels, 1); // not one per direction
}

// Nothing times an initial load out, so a backend that goes away mid-load
// leaves "init" in m_inflight. resetToBottom() is the way back out of that.
void TestChatModel::resetToBottomUnwedgesALostInitialLoad() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // issues the initial history as token 1
    QVERIFY(m.loadingOlder());

    // Token 1 is never answered - the reply died with the backend.
    m.resetToBottom(); // drops the stuck init and re-issues it as token 2

    // A late token 1 must land nowhere: cancelDir dropped it with the direction.
    m.handleResult(1, msgs(R"([{"timestamp":50}])"));
    QCOMPARE(m.rowCount(), 0);

    // Token 2 only exists if init was cleared before loadInitial() ran again.
    m.handleResult(2, msgs(R"([{"timestamp":100}])"));
    QCOMPARE(m.rowCount(), 1);
    QVERIFY(!m.loadingOlder());
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
    // ...and inside <pre> they are already literal. It wraps all the same, so
    // a long line stays inside the bubble instead of painting out of it.
    QCOMPARE(messageMarkup("a\n  b",
                           spans(R"([{"type":"preformatted","offset":0,"length":5}])"), kQuote),
             QString("<pre style=\"white-space:pre-wrap\">a\n  b</pre>"));
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

// Opening a chat and jumping into it happen in one go when a search hit is
// clicked: the chat switch fires the newest page, the jump follows immediately.
// Both are out at once, and the later instruction has to win.
void TestChatModel::gotoOutrunsTheOpeningPage() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");    // the opening page is token 1, still in flight
    m.gotoTimestamp(20); // token 2

    QSignalSpy anchored(&m, &ChatModel::anchored);
    m.handleResult(1, msgs(R"([{"timestamp":900},{"timestamp":800}])"));
    QCOMPARE(m.rowCount(), 0); // cancelled: that page is no longer wanted
    QVERIFY(!m.loadingOlder());

    m.handleResult(2, QJsonDocument::fromJson(R"({
        "anchor":20,"messages":[{"timestamp":10},{"timestamp":20}]})")
                          .object().toVariantMap());
    QCOMPARE(anchored.first().at(0).toLongLong(), 20LL);
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0), ChatModel::TimestampRole).toLongLong(), 20LL);
}

// Where a search matched is one more span over the same string, so it draws
// through the styling rather than replacing it - and it is the search's, not
// the message's, so it moves and clears without the row changing.
void TestChatModel::searchMarkJoinsTheStylingSpans() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.setQuoteColor(kQuote);
    m.setMatchColor("#ff0");
    m.handleResult(1, msgs(R"([
        {"timestamp":200,"content":{"type":"text","body":"hello there",
         "formatting":[{"type":"bold","offset":0,"length":5}]}},
        {"timestamp":100,"content":{"type":"text","body":"hello again"}}])"));
    QCOMPARE(m.data(m.index(0), ChatModel::MarkupRole).toString(),
             QString("<b>hello</b> there"));

    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    m.highlightMatches(200, spans(R"([{"offset":6,"length":5}])"));
    QCOMPARE(m.data(m.index(0), ChatModel::MarkupRole).toString(),
             QString("<b>hello</b> <span style=\"background-color:#ff0\">there</span>"));
    QCOMPARE(chg.count(), 1);
    // The other rows say nothing about a search they were not found by.
    QCOMPARE(m.data(m.index(1), ChatModel::MarkupRole).toString(), QString());

    // Stepping to the next hit unmarks the one behind it.
    m.highlightMatches(100, spans(R"([{"offset":0,"length":5}])"));
    QCOMPARE(m.data(m.index(0), ChatModel::MarkupRole).toString(),
             QString("<b>hello</b> there"));
    QVERIFY(m.data(m.index(1), ChatModel::MarkupRole).toString().contains("background-color"));

    // And closing the search takes it away without touching the styling.
    m.highlightMatches(0, {});
    QCOMPARE(m.data(m.index(1), ChatModel::MarkupRole).toString(), QString());
    QCOMPARE(m.data(m.index(0), ChatModel::MarkupRole).toString(),
             QString("<b>hello</b> there"));
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

    // A download says so, which is what tells the two apart on the way out.
    QCOMPARE(a.value("direction").toString(), QString("download"));

    // An upload keys on the message id, not the url, so one carrying this
    // row's url but another row's id is not this row's.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":7,
        "direction":"upload","state":"active","loaded":10,"total":99,
        "url":"https://h/a.png","localpath":"","thumbpath":"","error":""}])");
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

// A file we sent has a local path and no url until its upload lands, and the
// update comes back under the source it was asked with. Key on the other one
// and our own picture never shows a thumbnail.
void TestChatModel::anAttachmentWithNoUrlYetKeysOnItsPath() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(R"([{"timestamp":100,"is_outgoing":true,"from_jid":"me@h",
        "content":{"type":"media","attachments":[
            {"url":"","path":"/home/me/c.png","type":"image","name":"c.png"}]}}])"));

    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","url":"/home/me/c.png","localpath":"/home/me/c.png",
        "thumbpath":"/cache/c_320.png"}])");

    QCOMPARE(att0(m).value("thumburl").toUrl(), QUrl("file:///cache/c_320.png"));
    QCOMPARE(att0(m).value("localpath").toString(), QString("/home/me/c.png"));
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
            {"url":"","path":"/home/me/c.png","type":"image","name":"c.png"}]}}
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
    // Unbound by any view, so tacky is asked for its own default size.
    QCOMPARE(first.value("thumbmax").toInt(), 320);
    QCOMPARE(downloads.at(1).toMap().value("url").toString(), QString("https://h/a.png"));
    // Our own send is exempt from the policy, and has only a local file to
    // name until its upload lands.
    const QVariantMap own = downloads.at(2).toMap();
    QCOMPARE(own.value("auto").toInt(), 0);
    QCOMPARE(own.value("path").toString(), QString("/home/me/c.png"));
    QVERIFY(!own.contains("url"));

    // Tapping a held-back image asks again, this time ungated.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"idle","url":"https://h/a.png","error":""}])");
    sent.clear();
    m.loadAttachment(300, 0);
    QCOMPARE(sent.count(), 1);
    QVERIFY(!sent.first().at(2).toMap().contains("auto"));
}

// Every request that can end in a rendered thumbnail carries the size, not only
// the one the row fetches with.
void TestChatModel::thumbnailSizeFollowsTheView() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");

    QSignalSpy changed(&m, &ChatModel::thumbMaxChanged);
    m.setThumbMax(960);
    QCOMPARE(changed.count(), 1);
    // A screen not realised yet must not overwrite a good size.
    m.setThumbMax(0);
    m.setThumbMax(-1);
    QCOMPARE(m.thumbMax(), 960);
    QCOMPARE(changed.count(), 1);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.applyBatch(msgs(kMediaRow));
    m.loadAttachment(100, 0);
    m.openAttachment(100, 0);

    int asked = 0;
    for (const QList<QVariant> &call : sent) {
        if (call.at(0).toString() != "file" || call.at(1).toString() != "download")
            continue;
        QCOMPARE(call.at(2).toMap().value("thumbmax").toInt(), 960);
        ++asked;
    }
    QCOMPARE(asked, 3);
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

// An outgoing share before its PUT is through: the row carries a local path.
static const char *kUploadingRow = R"([{"timestamp":100,"is_outgoing":true,
    "from_jid":"me@h","server_status":"uploading",
    "content":{"type":"media","caption":"",
    "attachments":[{"url":"/home/me/a.png","type":"image","name":"a.png",
                    "size":1234,"mime":"image/png"}]}}])";

// Matched to its message by id, and it must not wipe what the local read that
// produced the thumbnail left behind.
void TestChatModel::uploadProgressReachesTheRowItBelongsTo() {
    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(kUploadingRow));

    // That read.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","url":"/home/me/a.png","localpath":"/home/me/a.png",
        "thumbpath":"/cache/a.png"}])");

    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":100,
        "direction":"upload","state":"active","loaded":10,"total":99,
        "url":"","localpath":"","thumbpath":"","error":""}])");

    const QVariantMap a = att0(m);
    QCOMPARE(a.value("state").toString(), QString("active"));
    QCOMPARE(a.value("direction").toString(), QString("upload"));
    QCOMPARE(a.value("loaded").toInt(), 10);
    QCOMPARE(a.value("total").toInt(), 99);
    // Still showing while its bytes go out.
    QCOMPARE(a.value("thumburl").toUrl(), QUrl("file:///cache/a.png"));
    QCOMPARE(chg.count(), 1);
    QCOMPARE(chg.first().at(2).value<QList<int>>(),
             QList<int>{ChatModel::AttachmentsRole});

    // An upload for a message this window is not showing changes nothing.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":999,
        "direction":"upload","state":"failed","error":"no upload service"}])");
    QCOMPARE(att0(m).value("state").toString(), QString("active"));

    // A confirm can relocate the row; the upload has to follow it.
    m.applyConfirmed(100, 300, QStringLiteral(""));
    QCOMPARE(att0(m).value("direction").toString(), QString("upload"));
}

void TestChatModel::sendFileHandsTackyThePath() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.sendFile(QUrl::fromLocalFile("/home/me/a.png"));
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("message"));
    QCOMPARE(sent.first().at(1).toString(), QString("sendFile"));
    const QVariantMap args = sent.first().at(2).toMap();
    QCOMPARE(args.value("acc").toString(), QString("me@h"));
    QCOMPARE(args.value("chat").toString(), QString("a@h"));
    // A path, not a url: tacky opens what it is handed from Tcl.
    QCOMPARE(args.value("path").toString(), QString("/home/me/a.png"));
    // Nothing else: the encrypt flag is tacky's to decide.
    QCOMPARE(args.size(), 3);

    // A dialog that was dismissed hands back nothing to send.
    sent.clear();
    m.sendFile(QUrl());
    QCOMPARE(sent.count(), 0);
}

// The extension is what everything after the copy reads the kind from, so a
// document the provider names without one is given the one its type implies.
void TestChatModel::aPickedDocumentIsNamedAfterItself() {
    const QUrl picked(
        "content://com.android.providers.media.documents/document/image%3A34");

    QCOMPARE(pickedfile::nameFor(picked, "sheet.png", "image/png"),
             QString("sheet.png"));
    QCOMPARE(pickedfile::nameFor(picked, "Screenshot", "image/png"),
             QString("Screenshot.png"));
    // Nothing to go on but the url, whose last segment is the provider's id.
    QCOMPARE(pickedfile::nameFor(picked, "", "image/png"),
             QString("image_34.png"));
    QCOMPARE(pickedfile::nameFor(picked, "", ""), QString("image_34"));

    // A name is a name: no directory of the provider's choosing comes with it.
    QCOMPARE(pickedfile::nameFor(picked, "../../etc/passwd", ""),
             QString("passwd"));
    QCOMPARE(pickedfile::nameFor(picked, ".profile", ""), QString("profile"));
    QCOMPARE(pickedfile::nameFor(QUrl("content://x/"), "", ""),
             QString("attachment"));

    // A local file is sent where it lies.
    QCOMPARE(pickedfile::localPath(QUrl::fromLocalFile("/home/me/a.png")),
             QString("/home/me/a.png"));
}

// A share's whole life, in order: the optimistic row, the local read behind
// its thumbnail, the PUT, then the send. One sequence because every step
// patches the same row, and what it must leave is an ordinary sent message.
void TestChatModel::anUploadedShareEndsUpSent() {
    ChatModel m;
    m.setAccount("me@h");
    m.setChat("a@h");

    feedEvent(m, R"(["event","message","New",{"acc":"me@h","jid":"a@h",
        "message":{"timestamp":100,"is_outgoing":true,"from_jid":"me@h",
        "server_status":"uploading","remote_status":"none","encryption":"omemo",
        "content":{"type":"media","caption":"","attachments":[
            {"url":"/home/me/a.png","type":"image","name":"a.png",
             "size":1234,"mime":"image/png"}]}}}])");
    QCOMPARE(m.rowCount(), 1);

    // The file module reads the local source in place to derive the thumbnail.
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","loaded":1234,"total":1234,"url":"/home/me/a.png",
        "localpath":"/home/me/a.png","thumbpath":"/cache/a.png","error":""}])");
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":100,
        "direction":"upload","state":"active","loaded":600,"total":1234,
        "url":"","localpath":"/home/me/a.png","thumbpath":"","error":""}])");
    QCOMPARE(att0(m).value("state").toString(), QString("active"));

    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":100,
        "direction":"upload","state":"done","loaded":1234,"total":1234,
        "url":"https://h/a.png","localpath":"/home/me/a.png","thumbpath":"",
        "error":""}])");
    // The send the finished PUT sets off, then the server's ack for it.
    feedEvent(m, R"(["event","message","Status",{"acc":"me@h","jid":"a@h",
        "timestamp":100,"server_status":"pending"}])");
    feedEvent(m, R"(["event","message","Confirmed",{"acc":"me@h","jid":"a@h",
        "timestamp":100,"newtimestamp":100,"server_status":""}])");

    QCOMPARE(m.data(m.index(0), ChatModel::ServerStatusRole).toString(), QString());
    // The stamp is not a casualty of the patches.
    QCOMPARE(m.data(m.index(0), ChatModel::EncryptionRole).toString(),
             QString("omemo"));
    // Nothing still transferring, and the picture still on screen.
    const QVariantMap a = att0(m);
    QCOMPARE(a.value("state").toString(), QString("done"));
    QCOMPARE(a.value("thumburl").toUrl(), QUrl("file:///cache/a.png"));
}

void TestChatModel::retryUploadNamesTheRow() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(kUploadingRow));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.retryUpload(100);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("message"));
    QCOMPARE(sent.first().at(1).toString(), QString("retryUpload"));
    const QVariantMap args = sent.first().at(2).toMap();
    QCOMPARE(args.value("chat").toString(), QString("a@h"));
    QCOMPARE(args.value("timestamp").toLongLong(), 100LL);
}

void TestChatModel::cancelUsesTheHandleTheTransferHas() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(R"([
        {"timestamp":200,"is_outgoing":false,"content":{"type":"media",
         "attachments":[{"url":"https://h/a.pdf","type":"file","name":"a.pdf"}]}},
        {"timestamp":100,"is_outgoing":true,"content":{"type":"media",
         "attachments":[{"url":"/home/me/b.pdf","type":"file","name":"b.pdf"}]}}
    ])"));
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","id":100,
        "direction":"upload","state":"active","loaded":10,"total":99}])");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    // A download is cancelled by url, which stops it for every row waiting on it.
    m.cancelAttachment(200, 0);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(1).toString(), QString("cancel"));
    QCOMPARE(sent.first().at(2).toMap().value("url").toString(),
             QString("https://h/a.pdf"));
    QVERIFY(!sent.first().at(2).toMap().contains("id"));

    // An upload has no shared url, so it goes by the message id instead.
    sent.clear();
    m.cancelAttachment(100, 0);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(2).toMap().value("id").toLongLong(), 100LL);
    QVERIFY(!sent.first().at(2).toMap().contains("url"));

    // Nothing pointed at is nothing to stop.
    sent.clear();
    m.cancelAttachment(999, 0);
    QCOMPARE(sent.count(), 0);
}

// tacky says nothing after deleting them, so the row is walked back here.
void TestChatModel::uncacheForgetsTheFileAndItsThumbnail() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h");
    m.applyBatch(msgs(kMediaRow));
    feedEvent(m, R"(["event","file","Update",{"acc":"me@h","direction":"download",
        "state":"done","url":"https://h/a.png","localpath":"/data/a.png",
        "thumbpath":"/cache/a.png"}])");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.uncacheAttachment(100, 0);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("file"));
    QCOMPARE(sent.first().at(1).toString(), QString("uncache"));
    QCOMPARE(sent.first().at(2).toMap().value("url").toString(),
             QString("https://h/a.png"));

    const QVariantMap a = att0(m);
    QVERIFY(a.value("thumburl").toUrl().isEmpty());
    QVERIFY(a.value("localpath").toString().isEmpty());
    // Back to the neutral end, which is what offers the fetch again.
    QCOMPARE(a.value("state").toString(), QString());
}

void TestChatModel::savingCopiesTheFileWhereItWasAsked() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString src = dir.filePath("a.png");
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("png bytes");
    f.close();

    ChatModel m;
    m.setAccount("me@h");
    m.applyBatch(msgs(kMediaRow));
    feedEvent(m, QByteArray(R"(["event","file","Update",{"acc":"me@h",
        "direction":"download","state":"done","url":"https://h/a.png",
        "localpath":")") + src.toUtf8() + R"("}])");

    QSignalSpy saved(&m, &ChatModel::attachmentSaved);
    const QString dest = dir.filePath("copy.png");
    m.saveAttachment(100, 0, QUrl::fromLocalFile(dest));
    QCOMPARE(saved.count(), 1);
    QCOMPARE(saved.first().at(0).toUrl(), QUrl::fromLocalFile(dest));
    QVERIFY(saved.takeFirst().at(1).toString().isEmpty());
    QCOMPARE(QFile(dest).size(), qint64(9));

    // The dialog asked about overwriting already, so the second save replaces
    // the first rather than refusing.
    m.saveAttachment(100, 0, QUrl::fromLocalFile(dest));
    QCOMPARE(saved.count(), 1);
    QVERIFY(saved.takeFirst().at(1).toString().isEmpty());

    // A save that failed quietly would read as one that worked.
    m.saveAttachment(100, 0, QUrl::fromLocalFile(dir.filePath("no/such/x.png")));
    QCOMPARE(saved.count(), 1);
    QVERIFY(!saved.takeFirst().at(1).toString().isEmpty());

    // And a dialog that was dismissed asked for nothing.
    m.saveAttachment(100, 0, QUrl());
    QCOMPARE(saved.count(), 0);
}

// Every resolve answers the same local path, so what it was for has to survive
// the round trip.
void TestChatModel::revealAnswersTheFolderTheFileIsIn() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // token 1: the initial history request
    m.applyBatch(msgs(kMediaRow));

    QSignalSpy folder(&m, &ChatModel::attachmentFolder);
    QSignalSpy opened(&m, &ChatModel::attachmentResolved);
    m.revealAttachment(100, 0); // token 2
    m.handleResult(2, QVariant(QString("/data/pics/a.png")));
    QCOMPARE(opened.count(), 0);
    QCOMPARE(folder.count(), 1);
    QCOMPARE(folder.takeFirst().at(0).toUrl(), QUrl("file:///data/pics"));

    // A fetch that came back with nothing has no folder to show either.
    m.revealAttachment(100, 0); // token 3
    m.handleResult(3, QVariant(QString()));
    QCOMPARE(folder.count(), 1);
    QVERIFY(folder.takeFirst().at(0).toUrl().isEmpty());

    // Nor has one the backend refused outright, which would otherwise leave
    // the request hanging.
    m.revealAttachment(100, 0); // token 4
    m.handleError(4, QStringLiteral("no such account"));
    QCOMPARE(folder.count(), 1);
    QVERIFY(folder.takeFirst().at(0).toUrl().isEmpty());
}

// The row's stanza is the one it arrived with, and tacky fills the store in
// afterwards for an upload or a parked send without saying so - so the viewer
// asks, and lays out what comes back.
void TestChatModel::rawXmlIsAskedOfTheStore() {
    TackyBackend backend;
    ChatModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setChat("a@h"); // token 1
    // Stored before its file went up, so the row is carrying nothing.
    m.applyBatch(msgs(R"([{"timestamp":100,"raw_xml":""}])"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy ready(&m, &ChatModel::rawXmlReady);
    QCOMPARE(m.requestRawXml(100), 2);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(1).toString(), QString("rawxml"));
    const QVariantMap args = sent.first().at(2).toMap();
    QCOMPARE(args.value("chat").toString(), QString("a@h"));
    QCOMPARE(args.value("timestamp").toLongLong(), 100LL);
    // Nothing to show until the store answers.
    QCOMPARE(ready.count(), 0);

    m.handleResult(2, QVariant(QString("<message to='a@h'><body>hi</body></message>")));
    QCOMPARE(ready.count(), 1);
    QCOMPARE(ready.first().at(0).toInt(), 2); // under the token that asked
    QCOMPARE(ready.takeFirst().at(1).toString(),
             QString("<message to=\"a@h\">\n  <body>hi</body>\n</message>"));

    // A message that never had a stanza built answers empty.
    QCOMPARE(m.requestRawXml(100), 3);
    m.handleResult(3, QVariant(QString()));
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.takeFirst().at(1).toString().isEmpty());

    // And so does a request the backend refuses.
    QCOMPARE(m.requestRawXml(100), 4);
    m.handleError(4, QStringLiteral("no such account"));
    QCOMPARE(ready.count(), 1);
    QVERIFY(ready.takeFirst().at(1).toString().isEmpty());

    // A late answer to a request the chat switch dropped opens nothing.
    QCOMPARE(m.requestRawXml(100), 5);
    m.setChat("b@h");
    m.handleResult(5, QVariant(QString("<message to='a@h'/>")));
    QCOMPARE(ready.count(), 0);

    // No backend, no token - and nothing for an answer to arrive on.
    ChatModel bare;
    QSignalSpy bareReady(&bare, &ChatModel::rawXmlReady);
    QCOMPARE(bare.requestRawXml(100), 0);
    QCOMPARE(bareReady.count(), 0);
}

// tacky stores the stanza on one line, unreadable at message length. Past the
// indenting, what matters is that an element holding only text keeps that text
// where it stood: that text is the body, and moving it rewrites the message.
void TestChatModel::xmlIsLaidOutOneElementPerLine() {
    const QString out = formatMessageXml(QStringLiteral(
        "<message xmlns='jabber:client' to='a@h' type='chat'>"
        "<body>hello there</body>"
        "<active xmlns='http://jabber.org/protocol/chatstates'/>"
        "<reply xmlns='urn:xmpp:reply:0' to='b@h' id='q'/>"
        "</message>"));
    QCOMPARE(out, QString("<message xmlns=\"jabber:client\" to=\"a@h\" type=\"chat\">\n"
                          "  <body>hello there</body>\n"
                          "  <active xmlns=\"http://jabber.org/protocol/chatstates\"/>\n"
                          "  <reply xmlns=\"urn:xmpp:reply:0\" to=\"b@h\" id=\"q\"/>\n"
                          "</message>"));

    // Escapes survive as escapes; a body reading `&lt;` in the record has to
    // still read `&lt;` here, or the viewer is misreporting the stanza.
    QCOMPARE(formatMessageXml(QStringLiteral("<message><body>a &lt; b &amp; c</body></message>")),
             QString("<message>\n  <body>a &lt; b &amp; c</body>\n</message>"));

    // Prefixes are shown, never resolved into names of Qt's own invention.
    QCOMPARE(formatMessageXml(QStringLiteral(
                 "<x:message xmlns:x='jabber:client'><x:body>hi</x:body></x:message>")),
             QString("<x:message xmlns:x=\"jabber:client\">\n"
                     "  <x:body>hi</x:body>\n"
                     "</x:message>"));

    // Nothing to lay out is not an error; it is what the empty state renders.
    QCOMPARE(formatMessageXml(QString()), QString());
}

// The stanza is a debug record, so it can be anything at all. Refusing to draw
// what will not parse would hide the very case worth looking at.
void TestChatModel::unparseableXmlIsShownAsItIs() {
    const QString truncated = QStringLiteral("<message to='a@h'><body>cut off");
    QCOMPARE(formatMessageXml(truncated), truncated);
    const QString notXml = QStringLiteral("nothing like a stanza");
    QCOMPARE(formatMessageXml(notXml), notXml);
}

QTEST_MAIN(TestChatModel)
#include "tst_chatmodel.moc"
