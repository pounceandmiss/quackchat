// Driven with canned tacky JSON, decoded exactly as TackyBackend decodes it.
// One integration test wires a real libtacky backend to prove
// setBackend/refresh/handleResult.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAbstractItemModelTester>
#include <QScopeGuard>
#include <QTranslator>

#include "ChatListModel.h"
#include "TackyBackend.h"

// Decode an ["event",module,name,args] string and route it into the model the
// same way TackyBackend would (element 3 -> QVariant).
static void feedEvent(ChatListModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QVariantList entriesFrom(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

class TestChatList : public QObject {
    Q_OBJECT
private slots:
    void sortsByActivity();
    void tiesGoByJidNotName();
    void pinsSitAboveActivity();
    void carriesUnreadCounts();
    void insertKeepsOrder();
    void upsertRenameInPlace();
    void upsertActivityReorders();
    void aReorderMovesTheRow();
    void removeDrops();
    void ignoresOtherAccounts();
    void integrationRefreshListsOnlySelf();
    void failedLoadIsNotAnEmptyList();
    void carriesMentionsAndRoomReason();
    void previewsTheNewestMessage();
    void aLongPreviewIsCutBetweenCharacters();
    void notesToSelfPreviewWithoutYou();
    void contactEditsGoOutAsRosterCommands();
    void roomEditsGoOutAsBookmarkCommands();
    void pinsGoOutAsChatlistCommands();
    void editsWithoutAnAccountOrJidAreDropped();
    void reloadReAsksTheServer();
};

// One outgoing frame as "module/method acc jid extra=...", for asserting on
// what an edit put on the wire.
static QStringList framesFrom(const QSignalSpy &sent) {
    QStringList out;
    for (const QList<QVariant> &call : sent) {
        const QVariantMap a = call.at(2).toMap();
        QStringList parts{call.at(0).toString() + "/" + call.at(1).toString()};
        for (auto it = a.constBegin(); it != a.constEnd(); ++it)
            parts << it.key() + "=" + it.value().toString();
        out << parts.join(' ');
    }
    return out;
}

void TestChatList::sortsByActivity() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"b@h","name":"Bob","last_activity":100,"order":100},
        {"jid":"a@h","name":"Al","last_activity":300,"order":300},
        {"jid":"c@h","name":"Cy","last_activity":200,"order":200}
    ])"));
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("a@h"));
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("c@h"));
    QCOMPARE(m.data(m.index(2), ChatListModel::JidRole).toString(), QString("b@h"));
}

// tacky's order is the whole rule, so contacts never messaged - all at order
// 0 - come out by JID, whatever they are named.
void TestChatList::tiesGoByJidNotName() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"zoe@h","name":"Abe","order":0},
        {"jid":"bob@h","name":"","order":0},
        {"jid":"amy@h","name":"Zed","order":0}
    ])"));
    QStringList order;
    for (int i = 0; i < m.rowCount(); ++i)
        order << m.data(m.index(i), ChatListModel::JidRole).toString();
    QCOMPARE(order, QStringList({"amy@h", "bob@h", "zoe@h"}));
}

// A pinned chat's order sits far above any activity, and it stays an int64
// on the way: rounded to a double, the two pins would tie.
void TestChatList::pinsSitAboveActivity() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"new@h","last_activity":1760000000000000,"order":1760000000000000},
        {"jid":"low@h","pinned":true,"order":4611686018427387905},
        {"jid":"top@h","pinned":true,"order":4611686018427387906}
    ])"));
    QStringList order;
    for (int i = 0; i < m.rowCount(); ++i)
        order << m.data(m.index(i), ChatListModel::JidRole).toString();
    QCOMPARE(order, QStringList({"top@h", "low@h", "new@h"}));
    QVERIFY(m.data(m.index(0), ChatListModel::PinnedRole).toBool());

    // Unpinned, it drops back to where its activity puts it.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"top@h","item":{"jid":"top@h","pinned":false,"order":5}}])");
    QCOMPARE(m.data(m.index(2), ChatListModel::JidRole).toString(), QString("top@h"));
}

void TestChatList::carriesUnreadCounts() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"order":300,"unread":3},
        {"jid":"b@h","last_activity":100,"order":100}
    ])"));
    QCOMPARE(m.data(m.index(0), ChatListModel::UnreadRole).toInt(), 3);
    // Absent means nothing unread, not "unknown" - the badge has to hide.
    QCOMPARE(m.data(m.index(1), ChatListModel::UnreadRole).toInt(), 0);
    QCOMPARE(m.roleNames().value(ChatListModel::UnreadRole), QByteArray("unread"));

    // Reading the chat re-emits the entry with the count cleared.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"a@h","item":{"jid":"a@h","last_activity":300,"order":300,"unread":0}}])");
    QCOMPARE(m.data(m.index(0), ChatListModel::UnreadRole).toInt(), 0);
}

void TestChatList::insertKeepsOrder() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"order":300},
        {"jid":"b@h","last_activity":100,"order":100}
    ])"));
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    // Activity 200 belongs between a (300) and b (100) -> row 1.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"x@h","item":{"jid":"x@h","last_activity":200,"order":200}}])");
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(ins.count(), 1);
    QCOMPARE(ins.first().at(1).toInt(), 1); // first inserted row
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("x@h"));
}

void TestChatList::upsertRenameInPlace() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([{"jid":"a@h","name":"Old","last_activity":300,"order":300}])"));
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    // Same activity -> position unchanged -> dataChanged, not insert/move.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"a@h","item":{"jid":"a@h","name":"New","last_activity":300,"order":300}}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(ins.count(), 0);
    QCOMPARE(chg.count(), 1);
    QCOMPARE(m.data(m.index(0), ChatListModel::NameRole).toString(), QString("New"));
}

void TestChatList::upsertActivityReorders() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"order":300},
        {"jid":"b@h","last_activity":200,"order":200},
        {"jid":"c@h","last_activity":100,"order":100}
    ])"));
    // c gets a new message -> activity 400 -> jumps to the top.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"c@h","item":{"jid":"c@h","last_activity":400,"order":400}}])");
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("c@h"));
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("a@h"));
    QCOMPARE(m.data(m.index(2), ChatListModel::JidRole).toString(), QString("b@h"));
}

// A move, so a view's current row follows the chat.
void TestChatList::aReorderMovesTheRow() {
    ChatListModel m;
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::QtTest);
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"order":300},
        {"jid":"b@h","last_activity":200,"order":200},
        {"jid":"c@h","last_activity":100,"order":100}
    ])"));
    QPersistentModelIndex c(m.index(2));
    QPersistentModelIndex a(m.index(0));
    QSignalSpy removed(&m, &QAbstractItemModel::rowsRemoved);
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"c@h","item":{"jid":"c@h","last_activity":400,"order":400}}])");
    QCOMPARE(removed.count(), 0);
    QCOMPARE(c.row(), 0);
    QCOMPARE(a.row(), 1);

    // And back down past the others.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"c@h","item":{"jid":"c@h","last_activity":50,"order":50}}])");
    QCOMPARE(c.row(), 2);
    QCOMPARE(a.row(), 0);
    QCOMPARE(m.data(c, ChatListModel::JidRole).toString(), QString("c@h"));
}

void TestChatList::removeDrops() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"order":300},
        {"jid":"b@h","last_activity":100,"order":100}
    ])"));
    QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
    feedEvent(m, R"(["event","chatlist","Remove",{"acc":"me@h","jid":"a@h"}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(rem.count(), 1);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("b@h"));
}

void TestChatList::ignoresOtherAccounts() {
    ChatListModel m;
    m.setAccount("me@h");
    // An event for a different account must not touch this model.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"other@h","jid":"x@h","item":{"jid":"x@h","last_activity":1,"order":1}}])");
    QCOMPARE(m.rowCount(), 0);
}

// Wire a real libtacky backend: add an in-memory account, then refresh() issues
// `chatlist get`, whose result must route through handleResult without error.
// A new account has one chat, the one with itself, and its fields arrive
// typed: a real bool, not the string "1".
void TestChatList::integrationRefreshListsOnlySelf() {
    TackyBackend backend;
    QVERIFY(backend.start());

    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    ChatListModel m;
    QSignalSpy applied(&m, &QAbstractItemModel::modelReset);
    m.setBackend(&backend);
    m.setAccount("me@example.com"); // triggers refresh -> chatlist get

    // Only an applied reply resets the model.
    QTRY_VERIFY2_WITH_TIMEOUT(applied.count() > 0, "no chatlist get reply applied", 5000);
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(),
             QString("me@example.com"));
    const QVariant self = m.data(m.index(0), ChatListModel::IsSelfRole);
    QCOMPARE(self.typeId(), QMetaType::Bool);
    QVERIFY(self.toBool());
    QCOMPARE(m.data(m.index(0), ChatListModel::PinnedRole).typeId(), QMetaType::Bool);
    QVERIFY(m.loadError().isEmpty());

    backend.stop();
}

// A failed `chatlist get` used to look exactly like a roster with nothing in
// it, which is how a schema error presented itself as "no conversations yet".
void TestChatList::failedLoadIsNotAnEmptyList() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);
    m.setAccount("me@h"); // issues `chatlist get` as token 1
    QCOMPARE(m.loadError(), QString());

    QSignalSpy failed(&m, &ChatListModel::loadErrorChanged);
    // Through the signal, not the handler: the bug was never connecting it.
    emit backend.error(1, "no such column: m.mentions_me");
    QCOMPARE(m.loadError(), QString("no such column: m.mentions_me"));
    QCOMPARE(failed.count(), 1);

    m.setAccount("other@h"); // token 2
    m.handleResult(2, QVariantList{});
    QCOMPARE(m.loadError(), QString()); // a good load clears it
}

// A mention is what makes a busy room worth looking at, and a failed join is
// only actionable if the row can say why - both ride along on the entry.
void TestChatList::carriesMentionsAndRoomReason() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"r@muc?join","unread":9,"unread_mentions":2,
         "room_state":"error","room_reason":"forbidden"}
    ])"));
    QCOMPARE(m.data(m.index(0), ChatListModel::UnreadMentionsRole).toInt(), 2);
    QCOMPARE(m.data(m.index(0), ChatListModel::RoomStateRole).toString(),
             QString("error"));
    QCOMPARE(m.data(m.index(0), ChatListModel::RoomReasonRole).toString(),
             QString("forbidden"));
    QCOMPARE(m.roleNames().value(ChatListModel::UnreadMentionsRole),
             QByteArray("unread_mentions"));
    QCOMPARE(m.roleNames().value(ChatListModel::RoomReasonRole),
             QByteArray("room_reason"));
}

// tacky hands over the newest message whole; the row wants one line of it.
void TestChatList::previewsTheNewestMessage() {
    // "%n attachment(s)" only reads as English through the source catalogue,
    // as it does in the app.
    QTranslator t;
    QVERIFY(t.load(QStringLiteral("quack_en"), QStringLiteral(":/i18n")));
    QVERIFY(QCoreApplication::installTranslator(&t));
    auto untranslate = qScopeGuard([&t] { QCoreApplication::removeTranslator(&t); });

    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":9,"order":9,"last_message":{"from_jid":"a@h/phone",
         "is_outgoing":false,"content":{"type":"text","body":"hey,\n  look"}}},
        {"jid":"b@h","last_activity":8,"order":8,"last_message":{"is_outgoing":true,
         "content":{"type":"text","body":"on my way"}}},
        {"jid":"r@muc?join","groupchat":true,"last_activity":7,"order":7,
         "last_message":{"from_jid":"r@muc/Romeo","is_outgoing":false,
         "content":{"type":"media","caption":"","attachments":[
           {"type":"image","name":"roof.jpg"}]}}},
        {"jid":"c@h","last_activity":6,"order":6,"last_message":{"retracted":true,
         "content":{"type":"text","body":""}}},
        {"jid":"d@h","last_activity":5,"order":5,"last_message":{"content":{"type":"media",
         "caption":"","attachments":[{"type":"file","name":"a.pdf"},
                                     {"type":"file","name":"b.pdf"}]}}},
        {"jid":"e@h","last_activity":4,"order":4,"last_message":{"content":{"type":"media",
         "caption":"","attachments":[{"type":"file","name":"notes.txt"}]}}},
        {"jid":"f@h","last_activity":0,"order":0}
    ])"));
    QStringList previews;
    for (int i = 0; i < m.rowCount(); ++i)
        previews << m.data(m.index(i), ChatListModel::PreviewRole).toString();
    // A 1:1 peer goes unnamed, the row being theirs; a chat with no history has
    // nothing to say, and the row falls back on its JID.
    QCOMPARE(previews, QStringList({"hey, look", "You: on my way",
                                    "Romeo: Photo", "Message deleted",
                                    "2 attachments", "notes.txt", ""}));
    QCOMPARE(m.roleNames().value(ChatListModel::PreviewRole), QByteArray("preview"));

    // A new message re-emits the entry, and the preview follows it.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"f@h","item":{"jid":"f@h","last_activity":10,"order":10,
         "last_message":{"is_outgoing":false,
         "content":{"type":"text","body":"<b>hi</b>"}}}}])");
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("f@h"));
    QCOMPARE(m.data(m.index(0), ChatListModel::PreviewRole).toString(),
             QString("<b>hi</b>"));
}

// Every message in the chat with yourself is yours, so "You:" says nothing.
void TestChatList::notesToSelfPreviewWithoutYou() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"me@h","is_self":true,"last_activity":2,"order":2,"last_message":{
         "is_outgoing":true,"content":{"type":"text","body":"buy milk"}}},
        {"jid":"b@h","last_activity":1,"order":1,"last_message":{"is_outgoing":true,
         "content":{"type":"text","body":"on my way"}}}
    ])"));
    QCOMPARE(m.data(m.index(0), ChatListModel::PreviewRole).toString(),
             QString("buy milk"));
    QVERIFY(m.data(m.index(0), ChatListModel::IsSelfRole).toBool());
    QCOMPARE(m.data(m.index(1), ChatListModel::PreviewRole).toString(),
             QString("You: on my way"));
    QVERIFY(!m.data(m.index(1), ChatListModel::IsSelfRole).toBool());
    QCOMPARE(m.roleNames().value(ChatListModel::IsSelfRole), QByteArray("is_self"));
}

void TestChatList::aLongPreviewIsCutBetweenCharacters() {
    ChatListModel m;
    m.setAccount("me@h");
    const QString body = QString(199, QLatin1Char('a')) + QStringLiteral("\U0001F986 and more");
    m.applyList({QVariantMap{
        {"jid", "a@h"}, {"last_activity", 1}, {"order", 1},
        {"last_message", QVariantMap{{"content", QVariantMap{{"type", "text"}, {"body", body}}}}}}});
    const QString preview = m.data(m.index(0), ChatListModel::PreviewRole).toString();
    QVERIFY(preview.isValidUtf16());
    QVERIFY(preview.startsWith(QString(199, QLatin1Char('a'))));
}

void TestChatList::contactEditsGoOutAsRosterCommands() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.addContact("bob@h", "Bob");
    m.addContact("cy@h", ""); // no name: none is sent, rather than an empty one
    m.renameContact("bob@h", "Bobby");
    m.removeContact("bob@h");

    QCOMPARE(framesFrom(sent),
             QStringList({"roster/add acc=me@h jid=bob@h name=Bob",
                          "roster/add acc=me@h jid=cy@h",
                          "roster/item acc=me@h jid=bob@h name=Bobby",
                          "roster/remove acc=me@h jid=bob@h"}));
}

// pin() and setPinned() only send commands; the rows move on tacky's <Item>s.
void TestChatList::pinsGoOutAsChatlistCommands() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.pin("room@muc?join", true);
    m.pin("bob@h", false);
    m.setPinned({"b@h", "a@h"});

    QCOMPARE(framesFrom(sent),
             QStringList({"chatlist/pin acc=me@h jid=room@muc?join pinned=true",
                          "chatlist/pin acc=me@h jid=bob@h pinned=false",
                          "chatlist/setPinned acc=me@h jids="}));
    QCOMPARE(sent.at(2).at(2).toMap().value("jids").toStringList(),
             QStringList({"b@h", "a@h"}));
    QCOMPARE(m.rowCount(), 0);
}

// The JID goes out exactly as the row holds it, `?join` and all - stripping it
// to the bare room JID is tacky's job, and doing it here as well would be a
// second place to get it wrong.
void TestChatList::roomEditsGoOutAsBookmarkCommands() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.joinRoom("rm@m.h?join", "romeo", "s3cret");
    m.joinRoom("bare@m.h", "", "");
    m.leaveRoom("rm@m.h?join");
    m.forceJoinRoom("rm@m.h?join");
    m.renameBookmark("rm@m.h?join", "The Room");
    m.removeBookmark("rm@m.h?join");

    // The nick and password ride along on the same bookmark write.
    const QString joinFrame = QStringLiteral(
        "bookmarks/item acc=me@h autojoin=1 jid=rm@m.h?join nick=romeo "
        "password=s3cret");
    QCOMPARE(framesFrom(sent),
             QStringList({
                 joinFrame,
                 "bookmarks/item acc=me@h autojoin=1 jid=bare@m.h",
                 "bookmarks/leave acc=me@h jid=rm@m.h?join",
                 "bookmarks/forceJoin acc=me@h jid=rm@m.h?join",
                 "bookmarks/item acc=me@h jid=rm@m.h?join name=The Room",
                 "bookmarks/remove acc=me@h jid=rm@m.h?join"}));
}

// A menu can outlive the row it was opened on, and the account pane starts with
// no account at all; neither may send a command that means "every contact".
void TestChatList::editsWithoutAnAccountOrJidAreDropped() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.removeContact("bob@h"); // no account yet
    QCOMPARE(sent.count(), 0);

    m.setAccount("me@h");
    sent.clear();
    m.removeContact("");
    m.removeBookmark("");
    m.renameContact("", "Bob");
    QCOMPARE(sent.count(), 0);
}

// refresh() re-reads what tacky already stored; Refresh in the UI means "ask
// the server again", which is two more commands.
void TestChatList::reloadReAsksTheServer() {
    TackyBackend backend;
    ChatListModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.reload();
    QCOMPARE(framesFrom(sent),
             QStringList({"roster/request acc=me@h",
                          "bookmarks/request acc=me@h",
                          "chatlist/get acc=me@h"}));
}

QTEST_MAIN(TestChatList)
#include "tst_chatlistmodel.moc"
