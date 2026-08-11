// Driven with canned tacky JSON, decoded exactly as TackyBackend decodes it.
// One integration test wires a real libtacky backend to prove
// setBackend/refresh/handleResult.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

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
    void unnamedChatsSortUnderTheirJid();
    void carriesUnreadCounts();
    void insertKeepsOrder();
    void upsertRenameInPlace();
    void upsertActivityReorders();
    void removeDrops();
    void ignoresOtherAccounts();
    void integrationRefreshEmpty();
    void failedLoadIsNotAnEmptyList();
    void carriesMentionsAndRoomReason();
    void contactEditsGoOutAsRosterCommands();
    void roomEditsGoOutAsBookmarkCommands();
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
        {"jid":"b@h","name":"Bob","last_activity":100},
        {"jid":"a@h","name":"Al","last_activity":300},
        {"jid":"c@h","name":"Cy","last_activity":200}
    ])"));
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("a@h"));
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("c@h"));
    QCOMPARE(m.data(m.index(2), ChatListModel::JidRole).toString(), QString("b@h"));
}

// Contacts never messaged all sit at activity 0, so the name leg orders that
// whole block. A nameless one belongs under the JID its row shows; a bare ""
// swept every one of them to the top instead.
void TestChatList::unnamedChatsSortUnderTheirJid() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"zoe@h","name":"","last_activity":0},
        {"jid":"bob@h","name":"Bob","last_activity":0},
        {"jid":"amy@h","name":"","last_activity":0},
        {"jid":"cy@h","name":"Cy","last_activity":0}
    ])"));
    QStringList order;
    for (int i = 0; i < m.rowCount(); ++i)
        order << m.data(m.index(i), ChatListModel::JidRole).toString();
    QCOMPARE(order, QStringList({"amy@h", "bob@h", "cy@h", "zoe@h"}));
}

void TestChatList::carriesUnreadCounts() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300,"unread":3},
        {"jid":"b@h","last_activity":100}
    ])"));
    QCOMPARE(m.data(m.index(0), ChatListModel::UnreadRole).toInt(), 3);
    // Absent means nothing unread, not "unknown" - the badge has to hide.
    QCOMPARE(m.data(m.index(1), ChatListModel::UnreadRole).toInt(), 0);
    QCOMPARE(m.roleNames().value(ChatListModel::UnreadRole), QByteArray("unread"));

    // Reading the chat re-emits the entry with the count cleared.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"a@h","item":{"jid":"a@h","last_activity":300,"unread":0}}])");
    QCOMPARE(m.data(m.index(0), ChatListModel::UnreadRole).toInt(), 0);
}

void TestChatList::insertKeepsOrder() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300},
        {"jid":"b@h","last_activity":100}
    ])"));
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    // Activity 200 belongs between a (300) and b (100) -> row 1.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"x@h","item":{"jid":"x@h","last_activity":200}}])");
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(ins.count(), 1);
    QCOMPARE(ins.first().at(1).toInt(), 1); // first inserted row
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("x@h"));
}

void TestChatList::upsertRenameInPlace() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([{"jid":"a@h","name":"Old","last_activity":300}])"));
    QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
    QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
    // Same activity -> position unchanged -> dataChanged, not insert/move.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"a@h","item":{"jid":"a@h","name":"New","last_activity":300}}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(ins.count(), 0);
    QCOMPARE(chg.count(), 1);
    QCOMPARE(m.data(m.index(0), ChatListModel::NameRole).toString(), QString("New"));
}

void TestChatList::upsertActivityReorders() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300},
        {"jid":"b@h","last_activity":200},
        {"jid":"c@h","last_activity":100}
    ])"));
    // c gets a new message -> activity 400 -> jumps to the top.
    feedEvent(m, R"(["event","chatlist","Item",
        {"acc":"me@h","jid":"c@h","item":{"jid":"c@h","last_activity":400}}])");
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), ChatListModel::JidRole).toString(), QString("c@h"));
    QCOMPARE(m.data(m.index(1), ChatListModel::JidRole).toString(), QString("a@h"));
    QCOMPARE(m.data(m.index(2), ChatListModel::JidRole).toString(), QString("b@h"));
}

void TestChatList::removeDrops() {
    ChatListModel m;
    m.setAccount("me@h");
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","last_activity":300},
        {"jid":"b@h","last_activity":100}
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
        {"acc":"other@h","jid":"x@h","item":{"jid":"x@h","last_activity":1}}])");
    QCOMPARE(m.rowCount(), 0);
}

// Wire a real libtacky backend: add an in-memory account, then refresh() issues
// `chatlist get`, whose (empty) result must route through handleResult without
// error and leave the model empty.
void TestChatList::integrationRefreshEmpty() {
    TackyBackend backend;
    QVERIFY(backend.start());

    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    ChatListModel m;
    QSignalSpy replies(&backend, &TackyBackend::result);
    m.setBackend(&backend);
    m.setAccount("me@example.com"); // triggers refresh -> chatlist get

    QVERIFY2(replies.wait(5000), "no chatlist get reply");
    QCOMPARE(m.rowCount(), 0);

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
