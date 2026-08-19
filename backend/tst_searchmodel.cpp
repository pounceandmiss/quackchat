#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "MessageMarkup.h"
#include "SearchModel.h"
#include "TackyBackend.h"

static QVariantMap res(const QByteArray &json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

// The last `message search` tacky was asked for, which is where the source, the
// cursor and the scope all show up.
static QVariantMap lastSearch(const QSignalSpy &sent) {
    QVariantMap out;
    for (const QList<QVariant> &call : sent)
        if (call.at(0).toString() == "message" && call.at(1).toString() == "search")
            out = call.at(2).toMap();
    return out;
}

// Scoped to one chat, with an archive that has already answered whether it can
// search - the state most cases below start from. Token 1 is that answer, so a
// scoped model's first search is token 2 and its first page-back token 3.
static void scopeTo(SearchModel &m, TackyBackend &b, const QString &chat,
                    bool remote = true) {
    m.setBackend(&b);
    m.setAccount("me@h");
    m.setChat(chat); // asks mam fulltextSupported as token 1
    m.handleResult(1, remote);
}

class TestSearchModel : public QObject {
    Q_OBJECT
private slots:
    void rowsComeOffTheResult();
    void searchReplacesTheDisplayedResults();
    void loadMoreAppendsBehindThem();
    void aFreshSearchKeepsTheRowsButNotTheCursor();
    void everyPageThatLandsIsAnnounced();
    void theStoreCursorPagesTheNextRequest();
    void accountWideCursorKeepsItsPair();
    void pagingNeverAsksTheServer();
    void serverLegNeedsAnArchiveThatCanSearch();
    void accountWideOmitsTheChat();
    void anEmptyCursorMeansComplete();
    void failedSearchSaysSoRatherThanShowingNothing();
    void aSearchThatErrorsOutrightStopsSpinning();
    void resultChatsAreDistinctAndOrdered();
    void snippetMarksTheRangesTackyReported();
    void snippetFlattensWithoutMovingTheRanges();
    void snippetMergesRangesThatTouch();
    void snippetCountsCodePoints();
};

void TestSearchModel::rowsComeOffTheResult() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();

    m.handleResult(2, res(R"({"messages":[
        {"timestamp":300,"chat_jid":"a@h","from_jid":"her@h","is_outgoing":false,
         "content":{"type":"text","body":"pizza tonight?",
                    "matches":[{"offset":0,"length":5}]}},
        {"timestamp":100,"chat_jid":"a@h","from_jid":"me@h","is_outgoing":true,
         "content":{"type":"media","caption":"the pizza","attachments":[],
                    "matches":[{"offset":4,"length":5}]}}],
        "complete":true,"last":100})"));

    QCOMPARE(m.rowCount(), 2);
    QVERIFY(!m.searching());
    QCOMPARE(m.data(m.index(0), SearchModel::TimestampRole).toLongLong(), 300);
    QCOMPARE(m.data(m.index(0), SearchModel::ChatJidRole).toString(), QString("a@h"));
    QCOMPARE(m.data(m.index(0), SearchModel::FromRole).toString(), QString("her@h"));
    QCOMPARE(m.data(m.index(0), SearchModel::BodyRole).toString(),
             QString("pizza tonight?"));
    QCOMPARE(m.data(m.index(0), SearchModel::SnippetRole).toString(),
             QString("<b>pizza</b> tonight?"));
    QVERIFY(!m.data(m.index(0), SearchModel::OutgoingRole).toBool());
    // A media hit matched on its caption, so that is the body it shows - and
    // the ranges index that same string.
    QCOMPARE(m.data(m.index(1), SearchModel::BodyRole).toString(), QString("the pizza"));
    QCOMPARE(m.data(m.index(1), SearchModel::SnippetRole).toString(),
             QString("the <b>pizza</b>"));
    QVERIFY(m.complete());
    QVERIFY(m.searched());
}

void TestSearchModel::searchReplacesTheDisplayedResults() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300})"));
    QCOMPARE(m.rowCount(), 1);

    // A second search is a fresh question, but the last answer holds the floor
    // until this one has something to say.
    m.setQuery("pasta");
    m.search();
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), SearchModel::TimestampRole).toLongLong(), 300);
    QVERIFY(m.searching());

    m.handleResult(3, res(R"({"messages":[{"timestamp":900,"chat_jid":"a@h"}],
                             "complete":true,"last":900})"));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), SearchModel::TimestampRole).toLongLong(), 900);
    // The first search's reply, arriving late, no longer has a window to land in.
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":true,"last":300})"));
    QCOMPARE(m.rowCount(), 1);
}

void TestSearchModel::loadMoreAppendsBehindThem() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300})"));
    m.loadMore();
    m.handleResult(3, res(R"({"messages":[{"timestamp":200,"chat_jid":"a@h"}],
                             "complete":true,"last":200})"));

    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0), SearchModel::TimestampRole).toLongLong(), 300);
    QCOMPARE(m.data(m.index(1), SearchModel::TimestampRole).toLongLong(), 200);
    QVERIFY(m.complete());
}

// The rows outlive the question they answered, but nothing else does - or the
// new search's first page would arrive as the old one's second.
void TestSearchModel::aFreshSearchKeepsTheRowsButNotTheCursor() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300})"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.setQuery("pasta");
    m.search();

    QCOMPARE(m.rowCount(), 1);
    const QVariantMap a = lastSearch(sent);
    QCOMPARE(a.value("query").toString(), QString("pasta"));
    QVERIFY(!a.contains("before"));
    // The last search ran out of pages; this one has not been asked yet.
    QVERIFY(!m.complete());
}

// What a view stepping through the hits waits on: three hits replaced by three
// others leaves the count where it was.
void TestSearchModel::everyPageThatLandsIsAnnounced() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    QSignalSpy arrived(&m, &SearchModel::resultsArrived);
    QSignalSpy counted(&m, &SearchModel::countChanged);
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":true,"last":300})"));
    QCOMPARE(arrived.count(), 1);

    counted.clear();
    m.setQuery("pasta");
    m.search();
    m.handleResult(3, res(R"({"messages":[{"timestamp":900,"chat_jid":"a@h"}],
                             "complete":true,"last":900})"));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(counted.count(), 0); // one hit for one hit
    QCOMPARE(arrived.count(), 2);
}

void TestSearchModel::theStoreCursorPagesTheNextRequest() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":1700000000123456})"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.loadMore();
    const QVariantMap a = lastSearch(sent);
    // Microseconds, so it needs the full width on the way back out.
    QCOMPARE(a.value("before").toLongLong(), 1700000000123456LL);
    QCOMPARE(a.value("query").toString(), QString("pizza"));
    QCOMPARE(a.value("chat").toString(), QString("a@h"));
    // Scoped, so there is one chat and no pair to send.
    QVERIFY(!a.contains("before_chat_jid"));
}

void TestSearchModel::accountWideCursorKeepsItsPair() {
    TackyBackend backend;
    SearchModel m;
    m.setBackend(&backend);
    m.setAccount("me@h"); // no chat: every chat in the account
    m.setQuery("pizza");
    m.search();
    // Equal timestamps in different chats are ordinary, so account-wide the
    // cursor is a pair. Dropping the chat resumes at the wrong row.
    m.handleResult(1, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300,"last_chat_jid":"a@h"})"));

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.loadMore();
    const QVariantMap a = lastSearch(sent);
    QCOMPARE(a.value("before").toLongLong(), 300LL);
    QCOMPARE(a.value("before_chat_jid").toString(), QString("a@h"));
}

void TestSearchModel::pagingNeverAsksTheServer() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setAlsoRemote(true);
    m.setQuery("pizza");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.search();
    QCOMPARE(lastSearch(sent).value("source").toString(), QString("both"));

    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300})"));
    sent.clear();
    m.loadMore();
    // tacky's `both` skips its remote half the moment a cursor is passed, so
    // asking for it again would be a local page wearing the wrong label.
    QCOMPARE(lastSearch(sent).value("source").toString(), QString("local"));
}

void TestSearchModel::serverLegNeedsAnArchiveThatCanSearch() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h", /*remote=*/false);
    QVERIFY(!m.remoteAvailable());

    m.setAlsoRemote(true); // asked for, but there is nothing to ask
    m.setQuery("pizza");
    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.search();
    QCOMPARE(lastSearch(sent).value("source").toString(), QString("local"));
}

void TestSearchModel::accountWideOmitsTheChat() {
    TackyBackend backend;
    SearchModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setAlsoRemote(true);
    m.setQuery("pizza");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.search();
    const QVariantMap a = lastSearch(sent);
    QVERIFY(!a.contains("chat"));
    // MAM queries one archive, so tacky errors on a remote source that names no
    // chat rather than downgrading it. Never offer what it would refuse.
    QVERIFY(!m.remoteAvailable());
    QCOMPARE(a.value("source").toString(), QString("local"));
}

void TestSearchModel::anEmptyCursorMeansComplete() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    // Nothing to page from - `complete` false with an empty cursor would leave
    // a Load more button whose only move is to repeat the same request.
    m.handleResult(2, res(R"({"messages":[],"complete":false,"last":null})"));
    QVERIFY(m.complete());
    QCOMPARE(m.rowCount(), 0);
    QVERIFY(m.searched());
    QVERIFY(!m.failed());
}

void TestSearchModel::failedSearchSaysSoRatherThanShowingNothing() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(2, res(R"({"messages":[{"timestamp":300,"chat_jid":"a@h"}],
                             "complete":false,"last":300})"));
    QCOMPARE(m.rowCount(), 1);

    // error/unsupported sit outside the declared JSON schema, so they arrive as
    // the string "1" rather than a bool - which is how they are written here.
    m.setQuery("pasta");
    m.search();
    m.handleResult(3, res(R"({"messages":[],"complete":false,"last":null,
                              "error":"1","unsupported":"1"})"));
    QVERIFY(m.failed());
    QVERIFY(m.complete()); // no cursor, so nothing to offer behind it
    // Nothing replaced them, so they would be read as this search's answer.
    QCOMPARE(m.rowCount(), 0);
}

// The other way a search fails: not a result carrying `error`, but the request
// itself refused or abandoned.
void TestSearchModel::aSearchThatErrorsOutrightStopsSpinning() {
    TackyBackend backend;
    SearchModel m;
    scopeTo(m, backend, "a@h");
    m.setQuery("pizza");
    m.search();
    QVERIFY(m.searching());

    // Through the signal, not the handler: the wiring is half the fix.
    emit backend.error(2, QStringLiteral("Account does not exist: a@h"));
    QVERIFY(!m.searching());
    QVERIFY(m.failed());
    QVERIFY(m.complete());
    QCOMPARE(m.rowCount(), 0);

    // And a late answer on that token cannot start it up again.
    m.handleResult(2, res(R"({"messages":[{"timestamp":1,"chat_jid":"a@h"}],
                             "complete":false,"last":1})"));
    QCOMPARE(m.rowCount(), 0);
}

void TestSearchModel::resultChatsAreDistinctAndOrdered() {
    TackyBackend backend;
    SearchModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setQuery("pizza");
    m.search();
    m.handleResult(1, res(R"({"messages":[
        {"timestamp":400,"chat_jid":"a@h"},
        {"timestamp":300,"chat_jid":"room@muc?join"},
        {"timestamp":200,"chat_jid":"a@h"}],
        "complete":true,"last":200,"last_chat_jid":"a@h"})"));
    // One name cache per chat, and this is the list the view builds them from.
    QCOMPARE(m.resultChats(), QStringList({"a@h", "room@muc?join"}));
}

// {offset, length} as tacky reports it, in code points.
static QVariantList at(std::initializer_list<QPair<int, int>> spans) {
    QVariantList out;
    for (const auto &s : spans)
        out.append(QVariantMap{{"offset", s.first}, {"length", s.second}});
    return out;
}

void TestSearchModel::snippetMarksTheRangesTackyReported() {
    // Every range, not just the first, and the body's own markup characters
    // stay text rather than turning into tags.
    QCOMPARE(searchSnippet("a <b> pizza and pizza", at({{6, 5}, {16, 5}})),
             QString("a &lt;b&gt; <b>pizza</b> and <b>pizza</b>"));
    // Whatever the backend matched on, marked as the body actually spells it.
    QCOMPARE(searchSnippet("Pizza", at({{0, 5}})), QString("<b>Pizza</b>"));
    // No ranges is a hit whose match isn't in the drawn string - an attachment
    // url, a stem the archive matched on - and the row still has to read.
    QCOMPARE(searchSnippet("plain enough", {}), QString("plain enough"));
}

void TestSearchModel::snippetFlattensWithoutMovingTheRanges() {
    // A result row is one line, however the message was laid out - and the
    // ranges index the message as written, so collapsing before marking would
    // put every mark after the first gap in the wrong place.
    QCOMPARE(searchSnippet("one\ntwo   three", at({{10, 5}})),
             QString("one two <b>three</b>"));
    // A range that starts in the collapsed gap marks from the first character
    // it actually reaches.
    QCOMPARE(searchSnippet("one\ntwo   three", at({{8, 5}})),
             QString("one two <b>thr</b>ee"));
    // A space between two matched characters stays inside the mark.
    QCOMPARE(searchSnippet("say hi to  you", at({{4, 5}})),
             QString("say <b>hi to</b> you"));
    // A range trailing off into whitespace does not drag the gap in with it -
    // a wash on a space at the end of a run reads as a stray blob.
    QCOMPARE(searchSnippet("say  hi  there", at({{5, 4}})),
             QString("say <b>hi</b> there"));
}

void TestSearchModel::snippetMergesRangesThatTouch() {
    // Two ranges over one run are one mark, not a tag inside its own kind.
    QCOMPARE(searchSnippet("aaaa", at({{0, 2}, {2, 2}})), QString("<b>aaaa</b>"));
    QCOMPARE(searchSnippet("aaaa", at({{0, 3}, {1, 3}})), QString("<b>aaaa</b>"));
    // A range running off the end is clamped rather than dropped.
    QCOMPARE(searchSnippet("ab", at({{1, 99}})), QString("a<b>b</b>"));
}

void TestSearchModel::snippetCountsCodePoints() {
    // tacky counts offsets the way Tcl counts characters, so an emoji ahead of
    // a range is one, not the two UTF-16 units QString would step over.
    QCOMPARE(searchSnippet(QString::fromUtf8("\U0001F600 pizza"), at({{2, 5}})),
             QString::fromUtf8("\U0001F600 <b>pizza</b>"));
}

QTEST_MAIN(TestSearchModel)
#include "tst_searchmodel.moc"
