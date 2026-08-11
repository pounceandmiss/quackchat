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
};

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

QTEST_MAIN(TestChatList)
#include "tst_chatlistmodel.moc"
