// The proxy between one window's list and the shared ChatListModel. Over a real
// ChatListModel rather than a stand-in: the roles it reads and the source order
// it leans on are that model's, and a stub would agree with itself.
#include <QtTest>
#include <QJsonDocument>
#include <QSignalSpy>

#include "ChatListFilter.h"
#include "ChatListModel.h"

static QVariantList entriesFrom(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

// The three chats every test here works from: one named late, one named early,
// one with no name at all.
static void seed(ChatListModel &m) {
    m.applyList(entriesFrom(R"([
        {"jid":"zoe@h","name":"Amy","last_activity":300},
        {"jid":"bob@h","name":"Bob","last_activity":200},
        {"jid":"cy@h","name":"","last_activity":100}
    ])"));
}

static QStringList jids(const QAbstractItemModel &m) {
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.index(i, 0).data(ChatListModel::JidRole).toString();
    return out;
}

class TestChatListFilter : public QObject {
    Q_OBJECT
private slots:
    void passesEverythingThroughByDefault();
    void matchesNameOrJidCaseInsensitively();
    void sortByNameUsesWhatTheRowShows();
    void switchingBackToRecentRestoresTheSourceOrder();
    void totalCountSeesPastTheFilter();
    void countFollowsTheFilterAndNotifies();
    void filteringOneViewLeavesTheModelAlone();
    void steppingWrapsAtBothEnds();
    void steppingCountsOnlyTheRowsOnShow();
    void entryAtHandsBackTheWholeRow();
};

void TestChatListFilter::passesEverythingThroughByDefault() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m);
    // Recent: the source's own order, newest first.
    QCOMPARE(jids(f), QStringList({"zoe@h", "bob@h", "cy@h"}));
}

void TestChatListFilter::matchesNameOrJidCaseInsensitively() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m);

    f.setQuery("am"); // the name, not the JID
    QCOMPARE(jids(f), QStringList({"zoe@h"}));

    f.setQuery("BO"); // bob@h by both, case ignored
    QCOMPARE(jids(f), QStringList({"bob@h"}));

    f.setQuery("cy"); // an unnamed chat still matches on its JID
    QCOMPARE(jids(f), QStringList({"cy@h"}));

    f.setQuery("@h"); // a substring, not a prefix
    QCOMPARE(f.rowCount(), 3);

    f.setQuery("");
    QCOMPARE(f.rowCount(), 3);
}

// An unnamed chat is listed by its JID, so that is what it sorts under - the
// model's own rule, and the reason cy@h lands between Amy and Bob.
void TestChatListFilter::sortByNameUsesWhatTheRowShows() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m);
    f.setSortMode(ChatListFilter::Name);
    QCOMPARE(jids(f), QStringList({"zoe@h", "bob@h", "cy@h"}));

    // Sorting and filtering compose: still by name, minus what is hidden.
    f.setQuery("@h");
    QCOMPARE(jids(f), QStringList({"zoe@h", "bob@h", "cy@h"}));
}

void TestChatListFilter::switchingBackToRecentRestoresTheSourceOrder() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"a@h","name":"Zed","last_activity":300},
        {"jid":"b@h","name":"Abe","last_activity":100}
    ])"));
    ChatListFilter f;
    f.setSource(&m);
    QCOMPARE(jids(f), QStringList({"a@h", "b@h"}));

    f.setSortMode(ChatListFilter::Name);
    QCOMPARE(jids(f), QStringList({"b@h", "a@h"}));

    f.setSortMode(ChatListFilter::Recent);
    QCOMPARE(jids(f), QStringList({"a@h", "b@h"}));
}

// The list has to tell "no conversations" from "none matching what you typed",
// and rowCount() alone cannot.
void TestChatListFilter::totalCountSeesPastTheFilter() {
    ChatListModel m;
    ChatListFilter f;
    f.setSource(&m);
    QSignalSpy total(&f, &ChatListFilter::totalCountChanged);

    seed(m); // a reset, not an insert
    QVERIFY(total.count() > 0);
    QCOMPARE(f.totalCount(), 3);

    f.setQuery("nothing matches this");
    QCOMPARE(f.rowCount(), 0);
    QCOMPARE(f.totalCount(), 3);
}

// Every window on an account shares one ChatListModel, which is the whole
// reason the filter is a proxy: typing in one list must not touch another.
// A heading above the rows is sized from this, so it has to notify on the
// filter's own account and not just on the source's.
void TestChatListFilter::countFollowsTheFilterAndNotifies() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m);
    QCOMPARE(f.count(), 3);

    QSignalSpy changed(&f, &ChatListFilter::countChanged);
    f.setQuery("am");
    QCOMPARE(f.count(), 1);
    QVERIFY(changed.count() > 0);

    // Nothing left is still a change, and it arrives as one.
    changed.clear();
    f.setQuery("nothing matches this");
    QCOMPARE(f.count(), 0);
    QVERIFY(changed.count() > 0);

    changed.clear();
    f.setQuery("");
    QCOMPARE(f.count(), 3);
    QVERIFY(changed.count() > 0);
}

void TestChatListFilter::filteringOneViewLeavesTheModelAlone() {
    ChatListModel m;
    seed(m);
    ChatListFilter one;
    ChatListFilter two;
    one.setSource(&m);
    two.setSource(&m);

    one.setQuery("bob");
    one.setSortMode(ChatListFilter::Name);
    QCOMPARE(one.rowCount(), 1);
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(jids(two), QStringList({"zoe@h", "bob@h", "cy@h"}));
}

// Ctrl+Tab's walk over the list. Off either end is the other end.
void TestChatListFilter::steppingWrapsAtBothEnds() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m); // zoe@h, bob@h, cy@h

    QCOMPARE(f.stepRow("zoe@h", 1), 1);
    QCOMPARE(f.stepRow("bob@h", 1), 2);
    QCOMPARE(f.stepRow("cy@h", 1), 0);   // off the foot, back to the head
    QCOMPARE(f.stepRow("zoe@h", -1), 2); // and off the head, back to the foot
    QCOMPARE(f.stepRow("cy@h", -1), 1);

    // With no chat open the step comes in from the near end.
    QCOMPARE(f.stepRow("", 1), 0);
    QCOMPARE(f.stepRow("", -1), 2);

    // The order stepped is the one on show, not the model's.
    f.setSortMode(ChatListFilter::Name);
    QCOMPARE(jids(f), QStringList({"zoe@h", "bob@h", "cy@h"})); // Amy, Bob, cy@h
    QCOMPARE(f.stepRow("zoe@h", 1), 1);

    // Nothing to step to: an empty list has no row to name.
    ChatListModel empty;
    ChatListFilter g;
    g.setSource(&empty);
    QCOMPARE(g.stepRow("", 1), -1);
    QCOMPARE(g.stepRow("zoe@h", -1), -1);
}

// A chat the search box hides is counted from as if nothing were open, not
// from where it used to sit.
void TestChatListFilter::steppingCountsOnlyTheRowsOnShow() {
    ChatListModel m;
    seed(m);
    ChatListFilter f;
    f.setSource(&m);
    f.setQuery("b"); // bob@h alone

    QCOMPARE(f.rowCount(), 1);
    QCOMPARE(f.stepRow("zoe@h", 1), 0);
    QCOMPARE(f.stepRow("zoe@h", -1), 0);
    // One row is its own neighbour in both directions.
    QCOMPARE(f.stepRow("bob@h", 1), 0);
    QCOMPARE(f.stepRow("bob@h", -1), 0);

    f.setQuery("nothing matches this");
    QCOMPARE(f.stepRow("bob@h", 1), -1);
}

// A chat is opened by JID, name and whether it is a room, so the whole entry
// is what the view needs back.
void TestChatListFilter::entryAtHandsBackTheWholeRow() {
    ChatListModel m;
    m.applyList(entriesFrom(R"([
        {"jid":"room@conf.h","name":"Room","groupchat":true,"last_activity":300},
        {"jid":"bob@h","name":"Bob","last_activity":200}
    ])"));
    ChatListFilter f;
    f.setSource(&m);

    const QVariantMap room = f.entryAt(0);
    QCOMPARE(room.value("jid").toString(), QString("room@conf.h"));
    QCOMPARE(room.value("name").toString(), QString("Room"));
    QVERIFY(room.value("groupchat").toBool());
    QCOMPARE(f.entryAt(1).value("jid").toString(), QString("bob@h"));

    // Off either end is empty, not a crash.
    QVERIFY(f.entryAt(-1).isEmpty());
    QVERIFY(f.entryAt(2).isEmpty());
}

QTEST_MAIN(TestChatListFilter)
#include "tst_chatlistfilter.moc"
