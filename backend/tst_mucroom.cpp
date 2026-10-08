// Driven with canned tacky replies and events, decoded exactly as TackyBackend
// decodes them. tacky decides who the people are, their order and their
// groups; what is under test is the row bookkeeping: a room's list moves under
// a presence several times a minute, and the details page is scrolled while it
// does, so the model has to move one row rather than replace the list.
#include <QtTest>
#include <QJsonDocument>
#include <QSignalSpy>

#include "MucRoomModel.h"
#include "TackyBackend.h"

static QVariantMap mapFrom(const QByteArray &json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

// One person as `muc people` gives it.
static QVariantMap person(const QString &nick, const QString &group,
                          const QString &realJid = {},
                          const QString &show = {}) {
    return QVariantMap{{"key", realJid.isEmpty() ? "nick:" + nick : realJid},
                       {"nick", nick},
                       {"jid", realJid},
                       {"occupant", nick.isEmpty() ? QString() : "room@muc.h/" + nick},
                       {"present", group != "absent"},
                       {"self", false},
                       {"group", group},
                       {"role", group == "absent" ? "none" : group},
                       {"affiliation", "none"},
                       {"show", show},
                       {"status", QString()},
                       {"caps", QVariantMap{}},
                       {"keys", QVariantMap{}}};
}

static QVariantMap people(const QVariantList &list,
                          const QVariantMap &groups = {},
                          const QVariantMap &me = {}) {
    return QVariantMap{{"list", "complete"}, {"groups", groups},
                       {"me", me}, {"people", list}};
}

static QStringList keys(const MucRoomModel &m) {
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i), MucRoomModel::KeyRole).toString();
    return out;
}

class TestMucRoom : public QObject {
    Q_OBJECT
private slots:
    void rowsComeInTheOrderGiven();
    void theJoinSuffixIsNotPartOfTheRoom();
    void aChangeMovesOneRow();
    void aMovedPersonMovesTheirRow();
    void leavingEmptiesTheRoom();
    void ourOwnPersonSpeaksForUs();
    void theFilterNarrowsTheRowsNotTheRoom();
    void readsOnlyWhileActive();
    void anotherRoomsEventsAreNotOurs();
    void aRefusedActionSaysWhichOneItWas();
    void anAffiliationNeedsARealJid();
    void integrationARoomNotJoinedHasNoPeople();
};

// tacky orders and counts; the rows are its answer as it stands.
void TestMucRoom::rowsComeInTheOrderGiven() {
    MucRoomModel m;
    m.applyPeople(people({person("mo", "moderator", "mo@h"),
                          person("zoe", "participant"),
                          person({}, "absent", "gone@h")},
                         {{"moderator", 1}, {"participant", 1}, {"absent", 1}}));
    QCOMPARE(keys(m), QStringList({"mo@h", "nick:zoe", "gone@h"}));
    QCOMPARE(m.groupCounts().value("absent").toInt(), 1);
    QVERIFY(!m.data(m.index(2), MucRoomModel::PresentRole).toBool());
    QCOMPARE(m.memberList(), QString("complete"));
}

// Chat JIDs carry ?join to mark them as a room's. The muc module keys its rooms
// by the JID underneath and would find nothing under the suffixed form.
void TestMucRoom::theJoinSuffixIsNotPartOfTheRoom() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setActive(true);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.setJid("room@muc.h?join");
    QCOMPARE(m.roomJid(), QString("room@muc.h"));
    QStringList methods;
    for (const auto &call : sent) {
        methods << call.at(1).toString();
        QCOMPARE(call.at(2).toMap().value("jid").toString(), QString("room@muc.h"));
    }
    QVERIFY(methods.contains("people"));
}

// A change to somebody already listed changes their row where it stands.
// Resetting the model instead would throw the scroll position away, which in a
// busy room happens several times a minute.
void TestMucRoom::aChangeMovesOneRow() {
    MucRoomModel m;
    m.applyPeople(people({person("amy", "participant"), person("zoe", "participant")}));

    QSignalSpy changed(&m, &MucRoomModel::dataChanged);
    QSignalSpy inserted(&m, &MucRoomModel::rowsInserted);
    QSignalSpy removed(&m, &MucRoomModel::rowsRemoved);
    QSignalSpy reset(&m, &MucRoomModel::modelReset);

    m.applyPeople(people({person("amy", "participant", {}, "away"),
                          person("zoe", "participant")}));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 0);

    m.applyPeople(people({person("amy", "participant", {}, "away"),
                          person("mia", "participant"),
                          person("zoe", "participant")}));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 1);

    m.applyPeople(people({person("amy", "participant", {}, "away"),
                          person("zoe", "participant")}));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(reset.count(), 0);
}

// A promotion puts somebody elsewhere in tacky's order: their row moves there
// rather than going and coming back.
void TestMucRoom::aMovedPersonMovesTheirRow() {
    MucRoomModel m;
    m.applyPeople(people({person("amy", "participant"), person("zoe", "participant")}));
    QSignalSpy moved(&m, &MucRoomModel::rowsMoved);
    QSignalSpy removed(&m, &MucRoomModel::rowsRemoved);
    m.applyPeople(people({person("zoe", "moderator"), person("amy", "participant")}));
    QCOMPARE(keys(m), QStringList({"nick:zoe", "nick:amy"}));
    QCOMPARE(moved.count(), 1);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(m.data(m.index(0), MucRoomModel::GroupRole).toString(), QString("moderator"));
}

void TestMucRoom::leavingEmptiesTheRoom() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join");
    QVariantMap amy = person("amy", "participant");
    amy["self"] = true;
    m.applyPeople(people({amy}));
    m.applySubject("about ducks");
    m.applyJoined(true);

    m.handleEvent("muc", "Left",
                  mapFrom(R"({"acc":"me@h","jid":"room@muc.h","nick":"amy"})"));
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.subject(), QString());
    QCOMPARE(m.myNick(), QString());
    QVERIFY(!m.joined());
}

// Who we are is the person tacky marks as us, so the card and the list cannot
// disagree; what we may do about the room is tacky's `me`.
void TestMucRoom::ourOwnPersonSpeaksForUs() {
    MucRoomModel m;
    QCOMPARE(m.myOccupantJid(), QString());
    QVariantMap amy = person("amy", "visitor");
    amy["self"] = true;
    QSignalSpy me(&m, &MucRoomModel::meChanged);
    m.applyPeople(people({person("zoe", "moderator"), amy}, {},
                         {{"request_voice", true}, {"destroy", false}}));
    QCOMPARE(me.count(), 1);
    QCOMPARE(m.myNick(), QString("amy"));
    QCOMPARE(m.myRole(), QString("visitor"));
    QCOMPARE(m.myOccupantJid(), QString("room@muc.h/amy"));
    QVERIFY(m.me().value("request_voice").toBool());

    // The same answer again says nothing new about us.
    m.applyPeople(people({person("zoe", "moderator"), amy}, {},
                         {{"request_voice", true}, {"destroy", false}}));
    QCOMPARE(me.count(), 1);
}

// The filter is a view over the room, not a subscription: clearing it brings
// everyone straight back without asking anything.
void TestMucRoom::theFilterNarrowsTheRowsNotTheRoom() {
    MucRoomModel m;
    m.applyPeople(people({person("amy", "participant", "amy@elsewhere.h"),
                          person("zoe", "participant")},
                         {{"participant", 2}}));

    m.setFilter("AM"); // case-insensitively, and on the nick
    QCOMPARE(keys(m), QStringList({"amy@elsewhere.h"}));
    QCOMPARE(m.total(), 2);
    QCOMPARE(m.groupCounts().value("participant").toInt(), 2);

    m.setFilter("elsewhere"); // and on the JID behind the nick
    QCOMPARE(m.rowCount(), 1);

    m.setFilter("");
    QCOMPARE(m.rowCount(), 2);
}

// The page is built with its chat, so a room nobody is looking at would read
// its people on every presence. Inactive, a change is only remembered; it is
// read once, on becoming active.
void TestMucRoom::readsOnlyWhileActive() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    QSignalSpy sent(&backend, &TackyBackend::sent);
    const auto peopleReads = [&] {
        int n = 0;
        for (const auto &call : sent)
            if (call.at(1).toString() == "people")
                ++n;
        return n;
    };
    m.setJid("room@muc.h?join");
    const QVariantMap changed = mapFrom(R"({"acc":"me@h","jid":"room@muc.h"})");
    m.handleEvent("muc", "PeopleChanged", changed);
    m.handleEvent("muc", "PeopleChanged", changed);
    QCOMPARE(peopleReads(), 0);

    m.setActive(true);
    QCOMPARE(peopleReads(), 1);
    m.handleEvent("muc", "PeopleChanged", changed);
    QCOMPARE(peopleReads(), 2);

    // Shown again with nothing changed meanwhile: nothing to read.
    m.setActive(false);
    m.setActive(true);
    QCOMPARE(peopleReads(), 2);
}

void TestMucRoom::anotherRoomsEventsAreNotOurs() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join");
    m.setActive(true);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    m.handleEvent("muc", "PeopleChanged",
                  mapFrom(R"({"acc":"me@h","jid":"other@muc.h"})"));
    m.handleEvent("muc", "PeopleChanged",
                  mapFrom(R"({"acc":"someone@else","jid":"room@muc.h"})"));
    QCOMPARE(sent.count(), 0);

    m.handleEvent("muc", "PeopleChanged",
                  mapFrom(R"({"acc":"me@h","jid":"room@muc.h"})"));
    QCOMPARE(sent.count(), 1);
}

// A moderation action can be refused ("not-allowed", "forbidden"), and the page
// has to be able to say which button that was.
void TestMucRoom::aRefusedActionSaysWhichOneItWas() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join"); // tokens 1-2: the two reads refresh() makes

    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy failed(&m, &MucRoomModel::actionFailed);

    m.kick("amy", "spam"); // token 3
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.at(0).at(1).toString(), QString("kick"));
    QCOMPARE(sent.at(0).at(2).toMap().value("reason").toString(), QString("spam"));

    emit backend.error(3, "not-allowed");
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), QString("Kick"));
    QCOMPARE(failed.at(0).at(1).toString(), QString("not-allowed"));

    // One that went through says nothing: the room reports what it did by
    // presence, and there is no second place to hear about it.
    m.setRole("amy", "moderator"); // token 4
    m.handleResult(4, {});
    QCOMPARE(failed.count(), 1);
    // A reply to that same token arriving twice finds nothing left to complain
    // about either.
    emit backend.error(4, "late");
    QCOMPARE(failed.count(), 1);
}

// Affiliations are written against a real JID, which a semi-anonymous room
// withholds. Without one there is nothing to send, and a blank -target would
// ask the room to ban itself.
void TestMucRoom::anAffiliationNeedsARealJid() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.setAffiliation("", "outcast", "why");
    QCOMPARE(sent.count(), 0);

    m.setAffiliation("amy@elsewhere.h", "outcast", "why");
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.at(0).at(1).toString(), QString("affiliation"));
    QCOMPARE(sent.at(0).at(2).toMap().value("target").toString(),
             QString("amy@elsewhere.h"));
    QCOMPARE(sent.at(0).at(2).toMap().value("jid").toString(),
             QString("room@muc.h"));
}

// Through the real backend, for the schema: a room we are not in answers with
// nobody, rather than an error.
void TestMucRoom::integrationARoomNotJoinedHasNoPeople() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"}, {"password", "x"},
                               {"domain", "example.com"}, {"username", "me"}});
    MucRoomModel m;
    QSignalSpy errors(&backend, &TackyBackend::error);
    QSignalSpy counted(&m, &MucRoomModel::countChanged);
    m.setBackend(&backend);
    m.setAccount("me@example.com");
    m.setJid("room@muc.example.com?join");
    m.setActive(true);
    QTRY_VERIFY(m.memberList() == "none");
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(errors.count(), 0);
    backend.stop();
}

QTEST_MAIN(TestMucRoom)
#include "tst_mucroom.moc"
