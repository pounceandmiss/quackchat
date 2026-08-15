// Driven with canned tacky replies and events, decoded exactly as TackyBackend
// decodes them. What is under test is the row bookkeeping: a room's list moves
// under a presence several times a minute, and the details page is scrolled
// while it does, so the model has to move one row rather than replace the list.
#include <QtTest>
#include <QJsonDocument>
#include <QSignalSpy>

#include "MucRoomModel.h"
#include "TackyBackend.h"

static QVariantMap mapFrom(const QByteArray &json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

// The shape `muc occupants` answers with, one entry per occupant.
static QVariantMap occupant(const QString &nick, const QString &role,
                            const QString &affiliation = QStringLiteral("none"),
                            const QString &realJid = {},
                            const QString &show = {}) {
    return QVariantMap{{"nick", nick},
                       {"role", role},
                       {"affiliation", affiliation},
                       {"jid", realJid},
                       {"show", show},
                       {"status", QString()},
                       {"caps", QVariantMap{}}};
}

static QStringList nicks(const MucRoomModel &m) {
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i)
        out << m.data(m.index(i), MucRoomModel::NickRole).toString();
    return out;
}

class TestMucRoom : public QObject {
    Q_OBJECT
private slots:
    void groupsByRoleThenNick();
    void theJoinSuffixIsNotPartOfTheRoom();
    void aPresenceMovesOneRow();
    void aRoleChangeMovesTheRowBetweenGroups();
    void leavingEmptiesTheRoom();
    void ourOwnRowIsMarkedAndSpeaksForUs();
    void theFilterNarrowsTheRowsNotTheRoom();
    void anotherRoomsEventsAreNotOurs();
    void aRefusedActionSaysWhichOneItWas();
    void anAffiliationNeedsARealJid();
    void halfAnOccupantJidIsNoJid();
};

// Moderators first, then participants, then visitors, and alphabetically
// within each - the grouping the Tk participant list draws.
void TestMucRoom::groupsByRoleThenNick() {
    MucRoomModel m;
    m.applyOccupants({occupant("zoe", "participant"),
                      occupant("Bob", "visitor"),
                      occupant("amy", "participant"),
                      occupant("mo", "moderator"),
                      occupant("ghost", "")});
    QCOMPARE(nicks(m), QStringList({"mo", "amy", "zoe", "Bob", "ghost"}));
    QCOMPARE(m.data(m.index(0), MucRoomModel::GroupRole).toString(),
             QString("moderator"));
    // A role the model has no group for is still somebody in the room.
    QCOMPARE(m.data(m.index(4), MucRoomModel::GroupRole).toString(),
             QString("other"));

    const QVariantMap counts = m.groupCounts();
    QCOMPARE(counts.value("moderator").toInt(), 1);
    QCOMPARE(counts.value("participant").toInt(), 2);
    QCOMPARE(counts.value("visitor").toInt(), 1);
    QCOMPARE(counts.value("other").toInt(), 1);
}

// Chat JIDs carry ?join to mark them as a room's. The muc module keys its rooms
// by the JID underneath and would find nothing under the suffixed form.
void TestMucRoom::theJoinSuffixIsNotPartOfTheRoom() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.setJid("room@muc.h?join");
    QCOMPARE(m.roomJid(), QString("room@muc.h"));
    QVERIFY(sent.count() >= 1);
    QCOMPARE(sent.at(0).at(0).toString(), QString("muc"));
    QCOMPARE(sent.at(0).at(1).toString(), QString("occupants"));
    QCOMPARE(sent.at(0).at(2).toMap().value("jid").toString(),
             QString("room@muc.h"));

    // And an occupant's avatar lives under the room JID plus their nick.
    m.applyOccupants({occupant("amy", "participant")});
    QCOMPARE(m.data(m.index(0), MucRoomModel::OccupantJidRole).toString(),
             QString("room@muc.h/amy"));
}

// A presence for somebody already listed changes their row where it stands.
// Resetting the model instead would throw the scroll position away, which in a
// busy room happens several times a minute.
void TestMucRoom::aPresenceMovesOneRow() {
    MucRoomModel m;
    m.applyOccupants({occupant("amy", "participant"), occupant("zoe", "participant")});

    QSignalSpy changed(&m, &MucRoomModel::dataChanged);
    QSignalSpy inserted(&m, &MucRoomModel::rowsInserted);
    QSignalSpy removed(&m, &MucRoomModel::rowsRemoved);

    m.applyOccupant(occupant("amy", "participant", "none", {}, "away"));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 0);
    QCOMPARE(m.data(m.index(0), MucRoomModel::ShowRole).toString(), QString("away"));

    // Somebody new arrives in the middle, and only that row is inserted.
    m.applyOccupant(occupant("mia", "participant"));
    QCOMPARE(nicks(m), QStringList({"amy", "mia", "zoe"}));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 1);

    m.removeOccupant("mia");
    QCOMPARE(nicks(m), QStringList({"amy", "zoe"}));
    QCOMPARE(removed.count(), 1);
}

// A promotion arrives as an ordinary presence, and has to leave the group it
// was in rather than be written over in place.
void TestMucRoom::aRoleChangeMovesTheRowBetweenGroups() {
    MucRoomModel m;
    m.applyOccupants({occupant("amy", "participant"), occupant("zoe", "participant")});
    m.applyOccupant(occupant("zoe", "moderator"));
    QCOMPARE(nicks(m), QStringList({"zoe", "amy"}));
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.groupCounts().value("participant").toInt(), 1);
    QCOMPARE(m.groupCounts().value("moderator").toInt(), 1);
}

void TestMucRoom::leavingEmptiesTheRoom() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join");
    m.applyOccupants({occupant("amy", "participant")});
    m.applyMyNick("amy");
    m.applySubject("about ducks");
    m.applyJoined(true);

    m.handleEvent("muc", "Left",
                  mapFrom(R"({"acc":"me@h","jid":"room@muc.h","nick":"amy"})"));
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.subject(), QString());
    QCOMPARE(m.myNick(), QString());
    QVERIFY(!m.joined());
}

// Our own role is read off our own row rather than asked for separately, so the
// two cannot disagree about what the list is drawing for us.
void TestMucRoom::ourOwnRowIsMarkedAndSpeaksForUs() {
    MucRoomModel m;
    m.applyOccupants({occupant("amy", "visitor"), occupant("zoe", "moderator")});
    QCOMPARE(m.myRole(), QString());

    QSignalSpy me(&m, &MucRoomModel::meChanged);
    m.applyMyNick("amy");
    QCOMPARE(me.count(), 1);
    QCOMPARE(m.myRole(), QString("visitor"));
    QCOMPARE(m.myAffiliation(), QString("none"));
    // zoe is row 0 (moderators first), so ours is the second.
    QVERIFY(!m.data(m.index(0), MucRoomModel::SelfRole).toBool());
    QVERIFY(m.data(m.index(1), MucRoomModel::SelfRole).toBool());

    // Granted voice: the same row, and our own standing moves with it.
    m.applyOccupant(occupant("amy", "participant"));
    QCOMPARE(m.myRole(), QString("participant"));
}

// The filter is a view over the room, not a subscription: clearing it brings
// everyone straight back without asking anything.
void TestMucRoom::theFilterNarrowsTheRowsNotTheRoom() {
    MucRoomModel m;
    m.applyOccupants({occupant("amy", "participant", "none", "amy@elsewhere.h"),
                      occupant("zoe", "participant")});

    m.setFilter("AM"); // case-insensitively, and on the nick
    QCOMPARE(nicks(m), QStringList({"amy"}));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.total(), 2);
    // Counted over the room rather than the rows: the heading is about the room.
    QCOMPARE(m.groupCounts().value("participant").toInt(), 2);

    m.setFilter("elsewhere"); // and on the JID behind the nick
    QCOMPARE(nicks(m), QStringList({"amy"}));

    m.setFilter("");
    QCOMPARE(nicks(m), QStringList({"amy", "zoe"}));
}

void TestMucRoom::anotherRoomsEventsAreNotOurs() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join");

    m.handleEvent("muc", "Presence", mapFrom(R"({
        "acc":"me@h","jid":"other@muc.h","nick":"amy",
        "occupant":{"nick":"amy","role":"participant","affiliation":"none"}})"));
    QCOMPARE(m.rowCount(), 0);

    m.handleEvent("muc", "Presence", mapFrom(R"({
        "acc":"someone@else","jid":"room@muc.h","nick":"amy",
        "occupant":{"nick":"amy","role":"participant","affiliation":"none"}})"));
    QCOMPARE(m.rowCount(), 0);

    m.handleEvent("muc", "Presence", mapFrom(R"({
        "acc":"me@h","jid":"room@muc.h","nick":"amy",
        "occupant":{"nick":"amy","role":"participant","affiliation":"none"}})"));
    QCOMPARE(m.rowCount(), 1);
}

// A moderation action can be refused ("not-allowed", "forbidden"), and the page
// has to be able to say which button that was.
void TestMucRoom::aRefusedActionSaysWhichOneItWas() {
    TackyBackend backend;
    MucRoomModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join"); // tokens 1-4: the four reads refresh() makes

    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy failed(&m, &MucRoomModel::actionFailed);

    m.kick("amy", "spam"); // token 5
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.at(0).at(1).toString(), QString("kick"));
    QCOMPARE(sent.at(0).at(2).toMap().value("reason").toString(), QString("spam"));

    emit backend.error(5, "not-allowed");
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.at(0).at(0).toString(), QString("Kick"));
    QCOMPARE(failed.at(0).at(1).toString(), QString("not-allowed"));

    // One that went through says nothing: the room reports what it did by
    // presence, and there is no second place to hear about it.
    m.setRole("amy", "moderator"); // token 6
    m.handleResult(6, {});
    QCOMPARE(failed.count(), 1);
    // A reply to that same token arriving twice finds nothing left to complain
    // about either.
    emit backend.error(6, "late");
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

// "room@svc/" and "/nick" are not JIDs, and tacky answers a request about one
// with an error rather than a miss. The page draws its own row outside the list
// from a card that is hidden until the nick lands - but a hidden item's
// bindings still run, so the empty case has to be empty rather than "/".
void TestMucRoom::halfAnOccupantJidIsNoJid() {
    MucRoomModel m;
    QCOMPARE(m.myOccupantJid(), QString()); // no room, no nick

    m.applyMyNick("amy");
    QCOMPARE(m.myOccupantJid(), QString()); // a nick, still no room

    TackyBackend backend;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.setJid("room@muc.h?join"); // which forgets the nick along with the room
    QCOMPARE(m.myOccupantJid(), QString());

    m.applyMyNick("amy");
    QCOMPARE(m.myOccupantJid(), QString("room@muc.h/amy"));

    // And the rows follow the same rule, since it is the same rule.
    m.applyOccupants({occupant("zoe", "participant")});
    QCOMPARE(m.data(m.index(0), MucRoomModel::OccupantJidRole).toString(),
             QString("room@muc.h/zoe"));
}

QTEST_MAIN(TestMucRoom)
#include "tst_mucroom.moc"
