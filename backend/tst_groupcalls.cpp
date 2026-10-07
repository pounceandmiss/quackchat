// A room's call from this side: the phase a window follows, the participants
// read off the call room's occupants and their legs, the re-seed after a
// reattach, and the wire shape of the reads against a real libtacky.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "CallsModel.h"
#include "GroupCall.h"
#include "GroupCallsModel.h"
#include "TackyBackend.h"

static void feed(GroupCallsModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QString peerState(GroupCallParticipants *p, int row) {
    return p->data(p->index(row), GroupCallParticipants::StateRole).toString();
}

static QString peerNick(GroupCallParticipants *p, int row) {
    return p->data(p->index(row), GroupCallParticipants::NickRole).toString();
}

// Occupants as `muc occupants` gives them: {nick, jid, call}.
static QVariantList occupants(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

// The last request that went out for `module`/`method`, or an empty map.
static QVariantMap lastSent(const QSignalSpy &sent, const QString &module,
                            const QString &method) {
    for (int i = sent.count() - 1; i >= 0; --i)
        if (sent.at(i).at(0).toString() == module &&
            sent.at(i).at(1).toString() == method)
            return sent.at(i).at(2).toMap();
    return {};
}

class TestGroupCalls : public QObject {
    Q_OBJECT
private slots:
    void callForNormalisesTheRoomAndReadsItsOccupants();
    void occupantsDriveTheRoomState();
    void aRoomsMucEventsRereadItOnce();
    void joinThenJoinedThenLeft();
    void joinAnInRoomCallInProgress();
    void answerAndDeclineAStoredInvite();
    void inACallItsOwnRoomIsRead();
    void leaveEndsLocallyAndKeepsOurReason();
    void aRefusedJoinEndsWithTheMessage();
    void occupantsAndSessionsMakeTheWall();
    void aHiddenJidStillShows();
    void aLegEndingOnItsOwnKeepsTheTile();
    void legVideoLandsOnTheTileAndPreviewOnTheCall();
    void theCallsOwnPreviewOutlivesItsLegs();
    void aLegPreviewGivesWayWhenItsLegEnds();
    void theListRestoresTheCallsPreview();
    void legEventsRouteBySidAndAccount();
    void callsModelLeavesLegsAlone();
    void theListSeedsTheSessions();
    void listOnConnectFindsUsInTheCall();
    void anInviteRingsWithItsStoredMessage();
    void dismissMakesRoomForANewJoin();
    // Against a real backend:
    void occupantsRoundTripThroughRealTacky();
    void startWithNoServiceEnds();
};

void TestGroupCalls::callForNormalisesTheRoomAndReadsItsOccupants() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    GroupCall *c = m.callFor("me@host", "Room@muc.host?join");
    QVERIFY(c);
    QCOMPARE(c->jid(), QString("room@muc.host"));
    QCOMPARE(c->account(), QString("me@host"));
    QCOMPARE(c->phase(), QString("idle"));
    QVERIFY(!c->active());
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), GroupCallsModel::CallRole).value<QObject *>(), c);

    const QVariantMap occ = lastSent(sent, "muc", "occupants");
    QCOMPARE(occ.value("acc").toString(), QString("me@host"));
    QCOMPARE(occ.value("jid").toString(), QString("room@muc.host"));
    QCOMPARE(lastSent(sent, "muc", "myNick").value("jid").toString(),
             QString("room@muc.host"));

    // One row per room however it is spelled.
    QCOMPARE(m.callFor("me@host", "room@muc.host"), c);
    QCOMPARE(m.callFor("me@host", "room@muc.host/nick"), c);
    QCOMPARE(m.rowCount(), 1);
    // Another account's view of the same room is another row.
    QVERIFY(m.callFor("alt@host", "room@muc.host") != c);
    QCOMPARE(m.rowCount(), 2);
    QVERIFY(!m.callFor("", "room@muc.host"));
    QVERIFY(!m.callFor("me@host", ""));
}

// An occupant is in the call while its `call` is set, and counts once it has
// announced. Out of a call none of them is a participant: they only make the
// banner.
void TestGroupCalls::occupantsDriveTheRoomState() {
    GroupCallsModel m;
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    QSignalSpy room(c, &GroupCall::roomChanged);
    c->applyOccupants(occupants(R"([
        {"nick":"me","jid":"me@host/a","call":{}},
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced","audio":true,"video":false}},
        {"nick":"cat","jid":"cat@host/x","call":{"state":"preparing","audio":true,"video":false}},
        {"nick":"dan","jid":"dan@host/x","call":""}
    ])"), "me");
    QVERIFY(c->active());
    QCOMPARE(c->count(), 1);
    QVERIFY(!c->joined());
    QCOMPARE(c->phase(), QString("idle"));
    QCOMPARE(c->participants()->rowCount(), 0);
    QCOMPARE(room.count(), 1);

    c->applyOccupants(occupants(R"([
        {"nick":"me","jid":"me@host/a","call":{"state":"announced","audio":true,"video":false}},
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced","audio":true,"video":false}}
    ])"), "me");
    QVERIFY(c->joined());
    QCOMPARE(c->count(), 2);
    QCOMPARE(room.count(), 2);

    c->applyOccupants({}, "me");
    QVERIFY(!c->active());
    QCOMPARE(room.count(), 3);
    // The same again is not a change.
    c->applyOccupants({}, "me");
    QCOMPARE(room.count(), 3);
}

// Joining a room replays everyone's presence; that is one read, not one each.
// Another room's news is not this one's.
void TestGroupCalls::aRoomsMucEventsRereadItOnce() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    m.callFor("me@host", "room@muc.host?join");
    QSignalSpy sent(&backend, &TackyBackend::sent);

    for (const char *nick : {"bob", "cat", "dan"})
        feed(m, QByteArray(R"(["event","muc","Presence",{"acc":"me@host","jid":"room@muc.host","nick":")") +
                    nick + R"("}])");
    feed(m, R"(["event","muc","Presence",{"acc":"me@host","jid":"other@muc.host","nick":"x"}])");
    feed(m, R"(["event","muc","Presence",{"acc":"alt@host","jid":"room@muc.host","nick":"x"}])");
    QCOMPARE(sent.count(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(sent.count(), 2);
    QCOMPARE(lastSent(sent, "muc", "occupants").value("jid").toString(),
             QString("room@muc.host"));

    // Tokens from 1: the first read was 1 and 2. The two answers land
    // together, in either order.
    backend.result(4, occupants(R"([
        {"nick":"me","jid":"me@host/a","call":{"state":"announced"}},
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced"}}
    ])"));
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(!c->active());
    backend.result(3, QString("me"));
    QVERIFY(c->active());
    QVERIFY(c->joined());
}

void TestGroupCalls::joinThenJoinedThenLeft() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    QSignalSpy phase(c, &GroupCall::phaseChanged);

    // No call on: one is started, in a room of its own.
    c->join(true);
    QCOMPARE(c->phase(), QString("joining"));
    QVERIFY(c->inCall());
    QVERIFY(c->video());
    QVERIFY(c->cameraOn());
    QCOMPARE(c->startedAt(), 0);
    const QVariantMap start = lastSent(sent, "groupcall", "start");
    QCOMPARE(start.value("chat").toString(), QString("room@muc.host"));
    QCOMPARE(start.value("video").toInt(), 1);
    // A second join while one is out changes nothing.
    c->join(false);
    QCOMPARE(sent.count(), 3); // myNick + occupants + start
    QVERIFY(c->video());

    feed(m, R"(["event","groupcall","Started",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host"}])");
    QCOMPARE(c->callJid(), QString("k3j9@muc.host"));
    // The call's own events name its room, and find their way here.
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"k3j9@muc.host"}])");
    QCOMPARE(c->phase(), QString("live"));
    QVERIFY(c->startedAt() > 0);
    // From now on the call's room is the one read.
    QCOMPARE(c->statusRoom(), QString("k3j9@muc.host"));
    QCoreApplication::processEvents();
    QCOMPARE(lastSent(sent, "muc", "occupants").value("jid").toString(),
             QString("k3j9@muc.host"));
    c->setVideo(false);
    QCOMPARE(lastSent(sent, "groupcall", "setVideo").value("jid").toString(),
             QString("k3j9@muc.host"));

    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"k3j9@muc.host","reason":"left the room"}])");
    QCOMPARE(c->phase(), QString("ended"));
    QVERIFY(!c->inCall());
    QCOMPARE(c->reason(), QString("left the room"));
    QCOMPARE(phase.count(), 3);
    // And back to the chat's own for the banner.
    QCoreApplication::processEvents();
    QCOMPARE(lastSent(sent, "muc", "occupants").value("jid").toString(),
             QString("room@muc.host"));
}

// A call held in the group chat itself (Movim's): its occupants say so, and
// join joins it there rather than starting another.
void TestGroupCalls::joinAnInRoomCallInProgress() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    GroupCall *c = m.callFor("me@host", "room@muc.host?join");
    c->applyOccupants(occupants(R"([
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced"}}
    ])"), "me");
    c->join(false);
    QCOMPARE(lastSent(sent, "groupcall", "join").value("jid").toString(),
             QString("room@muc.host"));
    QVERIFY(lastSent(sent, "groupcall", "start").isEmpty());
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host","chat":"room@muc.host"}])");
    QCOMPARE(c->callJid(), QString("room@muc.host"));
    QCOMPARE(c->phase(), QString("live"));
}

// The card and the ring answer by the stored message: its chat and timestamp.
void TestGroupCalls::answerAndDeclineAStoredInvite() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    GroupCall *c = m.callFor("me@host", "room@muc.host?join");
    c->answerInvite("room@muc.host?join", 42, "k3j9@muc.host", false);
    QCOMPARE(c->phase(), QString("joining"));
    QCOMPARE(c->callJid(), QString("k3j9@muc.host"));
    const QVariantMap join = lastSent(sent, "groupcall", "join");
    QCOMPARE(join.value("chat").toString(), QString("room@muc.host?join"));
    QCOMPARE(join.value("timestamp").toLongLong(), 42);
    QVERIFY(!join.contains("jid"));
    // Over by the time we tried: the reason, and the phase ended.
    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host",
         "reason":"the call has ended"}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString("the call has ended"));

    c->declineInvite("room@muc.host?join", 43);
    const QVariantMap no = lastSent(sent, "groupcall", "decline");
    QCOMPARE(no.value("chat").toString(), QString("room@muc.host?join"));
    QCOMPARE(no.value("timestamp").toLongLong(), 43);
}

// In a call started from a chat, news of the chat's own room is not news of
// our call; the call room's is.
void TestGroupCalls::inACallItsOwnRoomIsRead() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    c->join(false);
    feed(m, R"(["event","groupcall","Started",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host"}])");
    // Live before the loop turns: a backend that is not running fails every
    // request there, the start's included, and that would end a join.
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"k3j9@muc.host"}])");
    QCoreApplication::processEvents();
    QSignalSpy sent(&backend, &TackyBackend::sent);
    feed(m, R"(["event","muc","Presence",{"acc":"me@host","jid":"room@muc.host","nick":"bob"}])");
    QCoreApplication::processEvents();
    QCOMPARE(sent.count(), 0);
    feed(m, R"(["event","muc","Presence",{"acc":"me@host","jid":"k3j9@muc.host","nick":"x1"}])");
    QCoreApplication::processEvents();
    QCOMPARE(sent.count(), 2);
    QCOMPARE(lastSent(sent, "muc", "occupants").value("jid").toString(),
             QString("k3j9@muc.host"));
}

void TestGroupCalls::leaveEndsLocallyAndKeepsOurReason() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    c->join(false);
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"bob@host/x","sid":"tk-b","video":false}])");
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-b"}])");
    QCoreApplication::processEvents();

    QSignalSpy sent(&backend, &TackyBackend::sent);
    c->leave();
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString());
    const QVariantMap leave = lastSent(sent, "groupcall", "leave");
    QCOMPARE(leave.value("jid").toString(), QString("room@muc.host"));
    // Every tile is over with it.
    QCOMPARE(peerState(c->participants(), 0), QString("ended"));

    // The backend's <Left> that follows says "left"; a call we walked out of
    // has nothing to explain, so the reason stays empty.
    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"room@muc.host","reason":"left"}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString());

    // Nothing to leave twice.
    c->leave();
    QCOMPARE(sent.count(), 1);
}

// "not in room" comes back on the join's own token, and is the whole story:
// no <Joined> ever follows.
void TestGroupCalls::aRefusedJoinEndsWithTheMessage() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    c->join(false);
    // Tokens from 1: myNick and occupants were 1 and 2, the start is 3.
    backend.error(3, "join: not in room room@muc.host");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString("join: not in room room@muc.host"));
    // A read failing says nothing about the join.
    c->dismiss();
    c->join(false);
    backend.error(2, "whatever");
    QCOMPARE(c->phase(), QString("joining"));
}

// The tiles are the call room's occupants in the call, bar us; <Session> puts
// a leg on one, and that leg's calls events drive it.
void TestGroupCalls::occupantsAndSessionsMakeTheWall() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    GroupCallParticipants *p = c->participants();
    QSignalSpy count(p, &GroupCallParticipants::countChanged);

    const QByteArray both = R"([
        {"nick":"me","jid":"me@host/a","call":{"state":"announced"}},
        {"nick":"bob","jid":"bob@host/laptop","call":{"state":"announced","video":true}},
        {"nick":"cat","jid":"cat@host/phone","call":{"state":"preparing","video":false}},
        {"nick":"dan","jid":"dan@host/x","call":{}}
    ])";
    c->applyOccupants(occupants(both), "me");
    QCOMPARE(p->rowCount(), 2);
    QCOMPARE(count.count(), 2);
    QCOMPARE(c->count(), 2);
    QCOMPARE(peerNick(p, 0), QString("bob"));
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::JidRole).toString(),
             QString("bob@host"));
    QVERIFY(p->data(p->index(0), GroupCallParticipants::VideoRole).toBool());
    QVERIFY(!p->data(p->index(1), GroupCallParticipants::VideoRole).toBool());
    QCOMPARE(peerState(p, 0), QString("expected"));

    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"bob@host/laptop","sid":"tk-b","video":true}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"cat@host/phone","sid":"tk-c","video":false}])");
    QCOMPARE(p->rowCount(), 2);
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::SidRole).toString(),
             QString("tk-b"));
    QCOMPARE(peerState(p, 0), QString("connecting"));

    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-b"}])");
    QCOMPARE(peerState(p, 0), QString("active"));
    QCOMPARE(peerState(p, 1), QString("connecting"));
    QCOMPARE(p->activeCount(), 1);
    // A re-read keeps what the legs said.
    c->applyOccupants(occupants(both), "me");
    QCOMPARE(peerState(p, 0), QString("active"));

    // Gone from the call: the tile goes with them.
    c->applyOccupants(occupants(R"([
        {"nick":"me","jid":"me@host/a","call":{"state":"announced"}},
        {"nick":"cat","jid":"cat@host/phone","call":{"state":"announced"}}
    ])"), "me");
    QCOMPARE(p->rowCount(), 1);
    QCOMPARE(peerNick(p, 0), QString("cat"));

    // A later session to the same peer is the same tile, with a new leg.
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"cat@host/phone","sid":"tk-c2","video":false}])");
    QCOMPARE(p->rowCount(), 1);
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::SidRole).toString(),
             QString("tk-c2"));
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-c"}])");
    QCOMPARE(peerState(p, 0), QString("connecting")); // the old sid is nobody's
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-c2"}])");
    QCOMPARE(peerState(p, 0), QString("active"));
}

// An in-room call in a room that hides JIDs: no leg can reach them, but they
// are in the call and show as waiting.
void TestGroupCalls::aHiddenJidStillShows() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    c->applyOccupants(occupants(R"([
        {"nick":"dan","jid":"","call":{"state":"announced"}}
    ])"), "me");
    GroupCallParticipants *p = c->participants();
    QCOMPARE(p->rowCount(), 1);
    QCOMPARE(peerNick(p, 0), QString("dan"));
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::JidRole).toString(), QString());
    QCOMPARE(peerState(p, 0), QString("expected"));
}

// A leg that fails or ends while its owner is still in the call leaves the
// tile in place saying why: nothing redials, and the room still shows them.
void TestGroupCalls::aLegEndingOnItsOwnKeepsTheTile() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    GroupCallParticipants *p = c->participants();
    const QVariantList bob = occupants(R"([
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced"}}
    ])");
    c->applyOccupants(bob, "me");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"bob@host/x","sid":"tk-b","video":false}])");
    feed(m, R"(["event","calls","Warning",
        {"acc":"me@host","sid":"tk-b","reason":"ice restarting"}])");
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::WarningRole).toString(),
             QString("ice restarting"));
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-b"}])");
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::WarningRole).toString(),
             QString());

    feed(m, R"(["event","calls","Failed",
        {"acc":"me@host","sid":"tk-b","reason":"ice failed"}])");
    QCOMPARE(peerState(p, 0), QString("failed"));
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::ReasonRole).toString(),
             QString("ice failed"));
    // A re-read of the room, where they still are, keeps the tile as it was.
    c->applyOccupants(bob, "me");
    QCOMPARE(p->rowCount(), 1);
    QCOMPARE(peerState(p, 0), QString("failed"));
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::ReasonRole).toString(),
             QString("ice failed"));

    // Room-level warnings are the call's, not a tile's.
    feed(m, R"(["event","groupcall","Warning",
        {"acc":"me@host","jid":"room@muc.host","reason":"dan: JID hidden by the room, cannot connect"}])");
    QCOMPARE(c->warning(), QString("dan: JID hidden by the room, cannot connect"));
}

void TestGroupCalls::legVideoLandsOnTheTileAndPreviewOnTheCall() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    GroupCallParticipants *p = c->participants();
    QSignalSpy preview(c, &GroupCall::previewChanged);
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"bob@host/x","sid":"tk-b","video":true}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"cat@host/x","sid":"tk-c","video":true}])");

    feed(m, R"(["event","calls","VideoTrack",
        {"acc":"me@host","sid":"tk-b","mid":"video","direction":"incoming","name":"in-b"}])");
    QVERIFY(p->data(p->index(0), GroupCallParticipants::HasVideoRole).toBool());
    QCOMPARE(p->data(p->index(0), GroupCallParticipants::RemoteVideoRole).toMap()
                 .value("name").toString(),
             QString("in-b"));
    QVERIFY(!p->data(p->index(1), GroupCallParticipants::HasVideoRole).toBool());

    // Every leg previews the one camera; the first stream named is it and the
    // rest are the same picture.
    feed(m, R"(["event","calls","VideoPreview",
        {"acc":"me@host","sid":"tk-b","direction":"preview","name":"pv-b"}])");
    feed(m, R"(["event","calls","VideoPreview",
        {"acc":"me@host","sid":"tk-c","direction":"preview","name":"pv-c"}])");
    QVERIFY(c->sendingVideo());
    QCOMPARE(c->preview().value("name").toString(), QString("pv-b"));
    QCOMPARE(preview.count(), 1);

    feed(m, R"(["event","calls","VideoEnded",{"acc":"me@host","sid":"tk-b","mid":"video"}])");
    QVERIFY(!p->data(p->index(0), GroupCallParticipants::HasVideoRole).toBool());

    // <Left> takes every picture down, ours included.
    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"room@muc.host","reason":"disconnected"}])");
    QVERIFY(!c->sendingVideo());
    QCOMPARE(peerState(p, 1), QString("ended"));
}

// The call's own <VideoPreview> is the self-view for the whole call: legs'
// previews do not replace it, and no leg ending takes it down - the picture
// someone left alone in a call still sees.
void TestGroupCalls::theCallsOwnPreviewOutlivesItsLegs() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Started",{"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","VideoPreview",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host","name":"pv-call"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    QCOMPARE(c->preview().value("name").toString(), QString("pv-call"));
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"k3j9@muc.host","peer":"bob@host/x","sid":"tk-b","video":true}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"k3j9@muc.host","peer":"cat@host/x","sid":"tk-c","video":true}])");
    QSignalSpy preview(c, &GroupCall::previewChanged);
    feed(m, R"(["event","calls","VideoPreview",
        {"acc":"me@host","sid":"tk-b","direction":"preview","name":"pv-b"}])");
    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-b"}])");
    feed(m, R"(["event","calls","Failed",{"acc":"me@host","sid":"tk-c","reason":"ice failed"}])");
    QCOMPARE(c->preview().value("name").toString(), QString("pv-call"));
    QVERIFY(c->sendingVideo());
    QCOMPARE(preview.count(), 0);

    // Our leaving takes it down.
    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host","reason":"left"}])");
    QVERIFY(!c->sendingVideo());
}

// A backend without a call-wide self-view: a leg's stands in, and when that
// leg ends another live leg's takes over; the last one ending leaves none
// rather than a frozen picture.
void TestGroupCalls::aLegPreviewGivesWayWhenItsLegEnds() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    for (const char *nick : {"bob", "cat", "dan"})
        feed(m, QByteArray(R"(["event","groupcall","Session",{"acc":"me@host","jid":"room@muc.host","peer":")") +
                    nick + R"(@host/x","sid":"tk-)" + nick + R"(","video":true}])");
    for (const char *nick : {"bob", "cat", "dan"})
        feed(m, QByteArray(R"(["event","calls","VideoPreview",{"acc":"me@host","sid":"tk-)") +
                    nick + R"(","direction":"preview","name":"pv-)" + nick + R"("}])");
    QCOMPARE(c->preview().value("name").toString(), QString("pv-bob"));

    // A leg other than the one shown ending changes nothing.
    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-cat"}])");
    QCOMPARE(c->preview().value("name").toString(), QString("pv-bob"));

    // The shown one's ending hands over to one still live.
    feed(m, R"(["event","calls","Failed",{"acc":"me@host","sid":"tk-bob","reason":"gone"}])");
    QCOMPARE(c->preview().value("name").toString(), QString("pv-dan"));

    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-dan"}])");
    QVERIFY(!c->sendingVideo());
    QVERIFY(c->preview().isEmpty());
}

// A reattach learns the self-view from `groupcall list`, not only the event.
void TestGroupCalls::theListRestoresTheCallsPreview() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    feed(m, R"(["event","conn","State",{"acc":"me@host","state":"connected"}])");
    backend.result(1, QJsonDocument::fromJson(R"([
        {"jid":"k3j9@muc.host","chat":"room@muc.host","hosted":true,"count":1,
         "video":true,"mode":"mesh","preview":{"name":"pv-call"},"sessions":{}}
    ])").array().toVariantList());
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    QCOMPARE(c->preview().value("name").toString(), QString("pv-call"));
    // A leg's own preview arriving later does not replace it.
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host","peer":"bob@host/x","sid":"tk-b","video":true}])");
    feed(m, R"(["event","calls","VideoPreview",
        {"acc":"me@host","sid":"tk-b","direction":"preview","name":"pv-b"}])");
    QCOMPARE(c->preview().value("name").toString(), QString("pv-call"));
}

// A sid is the session's, and both ends of a call between two of our own
// accounts share it: the account picks the room.
void TestGroupCalls::legEventsRouteBySidAndAccount() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","Joined",{"acc":"a@host","jid":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","Joined",{"acc":"b@host","jid":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"a@host","jid":"room@muc.host","peer":"b@host/x","sid":"tk-s","video":false}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"b@host","jid":"room@muc.host","peer":"a@host/x","sid":"tk-s","video":false}])");
    GroupCall *a = m.find("a@host", "room@muc.host");
    GroupCall *b = m.find("b@host", "room@muc.host");
    feed(m, R"(["event","calls","Active",{"acc":"a@host","sid":"tk-s"}])");
    QCOMPARE(peerState(a->participants(), 0), QString("active"));
    QCOMPARE(peerState(b->participants(), 0), QString("connecting"));
    // No sid, no leg.
    feed(m, R"(["event","calls","Active",{"acc":"b@host"}])");
    QCOMPARE(peerState(b->participants(), 0), QString("connecting"));
}

// The legs are `calls` sessions and `calls list` names them, but they never
// announced themselves with <Outgoing>/<Incoming> and must not turn into 1:1
// call windows on a reattach.
void TestGroupCalls::callsModelLeavesLegsAlone() {
    TackyBackend backend;
    CallsModel calls;
    calls.setBackend(&backend);
    calls.handleEvent("conn", "State",
                      QVariantMap{{"acc", "me@host"}, {"state", "connected"}});
    calls.handleResult(
        1, QJsonDocument::fromJson(R"([
            {"sid":"tk-leg","peer":"bob@host","direction":"outgoing","state":"active",
             "peer_ringing":false,"group":"room@muc.host","video_local":false,"video_remote":false},
            {"sid":"tk-one","peer":"cat@host","direction":"outgoing","state":"active",
             "peer_ringing":false,"group":"","video_local":false,"video_remote":false}
        ])").array().toVariantList());
    QCOMPARE(calls.rowCount(), 1);
    QCOMPARE(calls.data(calls.index(0), CallsModel::SidRole).toString(),
             QString("tk-one"));
}

// A reattach: `groupcall list` names each session, and `calls list` how far
// each got; the occupants then put names to them.
void TestGroupCalls::theListSeedsTheSessions() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    feed(m, R"(["event","conn","State",{"acc":"me@host","state":"connected"}])");
    backend.result(1, QJsonDocument::fromJson(R"([
        {"jid":"k3j9@muc.host","chat":"room@muc.host","hosted":true,"count":3,
         "video":false,"mode":"mesh","preview":{},
         "sessions":{"bob@host/x":"tk-b","cat@host/x":"tk-c"}}
    ])").array().toVariantList());
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    GroupCallParticipants *p = c->participants();
    QCOMPARE(p->rowCount(), 2);
    QCOMPARE(peerState(p, 0), QString("connecting"));

    // Tokens from 1: list, the new row's read (2 and 3), then calls list.
    QCOMPARE(sent.last().at(0).toString(), QString("calls"));
    QCOMPARE(sent.last().at(1).toString(), QString("list"));
    backend.result(4, QJsonDocument::fromJson(R"([
        {"sid":"tk-b","peer":"bob@host","state":"active","group":"k3j9@muc.host"},
        {"sid":"tk-c","peer":"cat@host","state":"connecting","group":"k3j9@muc.host"},
        {"sid":"tk-one","peer":"eve@host","state":"active","group":""}
    ])").array().toVariantList());
    const int bob = p->indexOfKey("bob@host/x");
    const int cat = p->indexOfKey("cat@host/x");
    QCOMPARE(peerState(p, bob), QString("active"));
    QCOMPARE(peerState(p, cat), QString("connecting"));

    c->applyOccupants(occupants(R"([
        {"nick":"me","jid":"me@host/a","call":{"state":"announced"}},
        {"nick":"Bob","jid":"bob@host/x","call":{"state":"announced"}},
        {"nick":"Cat","jid":"cat@host/x","call":{"state":"announced"}}
    ])"), "me");
    QCOMPARE(p->rowCount(), 2);
    QCOMPARE(peerNick(p, bob), QString("Bob"));
    QCOMPARE(peerState(p, bob), QString("active"));

    // Then the leg's events pick up where the snapshot left off.
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-c"}])");
    QCOMPARE(peerState(p, cat), QString("active"));
}

// A UI that reattached to a backend still in a call: `groupcall list` on the
// account's connected edge names the room, which puts the phase at live.
void TestGroupCalls::listOnConnectFindsUsInTheCall() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    feed(m, R"(["event","conn","State",{"acc":"me@host","state":"connected"}])");
    const QVariantMap list = lastSent(sent, "groupcall", "list");
    QCOMPARE(list.value("acc").toString(), QString("me@host"));
    QCOMPARE(sent.count(), 1);

    backend.result(1, QJsonDocument::fromJson(R"([
        {"jid":"room@muc.host","count":3,"video":true,"mode":"mesh","sessions":{}}
    ])").array().toVariantList());
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    QCOMPARE(c->phase(), QString("live"));
    QVERIFY(c->video());
    QVERIFY(c->startedAt() > 0);
    QCoreApplication::processEvents();
    QCOMPARE(lastSent(sent, "muc", "occupants").value("jid").toString(),
             QString("room@muc.host"));
    // list, then the new row's read of the room.
    QCOMPARE(sent.count(), 3);

    // Every other state of the connection asks nothing.
    for (const char *s : {"connecting", "disconnected", "auth-error"})
        feed(m, QByteArray(R"(["event","conn","State",{"acc":"me@host","state":")") +
                    s + R"("}])");
    QCOMPARE(sent.count(), 3);

    // Already live: a later list is not a second join, and the reconnect
    // re-reads the room as well.
    QSignalSpy phase(c, &GroupCall::phaseChanged);
    feed(m, R"(["event","conn","State",{"acc":"me@host","state":"connected"}])");
    QCOMPARE(sent.count(), 6);
    backend.result(4, QJsonDocument::fromJson(R"([
        {"jid":"room@muc.host","count":3,"video":true,"mode":"mesh","sessions":{}}
    ])").array().toVariantList());
    QCoreApplication::processEvents();
    QCOMPARE(phase.count(), 0);
    QCOMPARE(sent.count(), 6);
}

void TestGroupCalls::anInviteRingsWithItsStoredMessage() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy invited(&m, &GroupCallsModel::invited);

    feed(m, R"(["event","groupcall","Invited",
        {"acc":"me@host","jid":"k3j9@muc.host","from":"bob@host",
         "chat":"Room@muc.host?join","timestamp":1790000000000000,"video":true}])");
    QCOMPARE(invited.count(), 1);
    const QList<QVariant> args = invited.first();
    QCOMPARE(args.at(0).toString(), QString("me@host"));
    QCOMPARE(args.at(1).toString(), QString("Room@muc.host?join"));
    QCOMPARE(args.at(2).toLongLong(), 1790000000000000LL);
    QCOMPARE(args.at(3).toString(), QString("k3j9@muc.host"));
    QCOMPARE(args.at(4).toString(), QString("bob@host"));
    QVERIFY(args.at(5).toBool());
    // The chat is a row by now, so the answer finds it.
    QVERIFY(m.find("me@host", "room@muc.host"));

    // The invite we send goes to the bare JID.
    GroupCall *c = m.find("me@host", "room@muc.host");
    c->invite(" Cat@host/phone ");
    const QVariantMap out = lastSent(sent, "groupcall", "invite");
    QCOMPARE(out.value("to").toString(), QString("Cat@host"));
    QCOMPARE(out.value("jid").toString(), QString("room@muc.host"));
}

void TestGroupCalls::dismissMakesRoomForANewJoin() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@host", "room@muc.host");
    c->join(false);
    feed(m, R"(["event","groupcall","Joined",{"acc":"me@host","jid":"room@muc.host"}])");
    feed(m, R"(["event","groupcall","Session",
        {"acc":"me@host","jid":"room@muc.host","peer":"bob@host/x","sid":"tk-b","video":false}])");
    feed(m, R"(["event","groupcall","Left",
        {"acc":"me@host","jid":"room@muc.host","reason":"disconnected"}])");
    QCOMPARE(c->phase(), QString("ended"));
    // Not while in a call.
    c->dismiss();
    QCOMPARE(c->phase(), QString("idle"));
    QCOMPARE(c->reason(), QString());
    QCOMPARE(c->participants()->rowCount(), 0);
    // The room's own state is untouched by ours.
    c->applyOccupants(occupants(R"([
        {"nick":"bob","jid":"bob@host/x","call":{"state":"announced"}}
    ])"), "me");
    QVERIFY(c->active());
    QCOMPARE(c->phase(), QString("idle"));

    c->join(false);
    QCOMPARE(c->phase(), QString("joining"));
    c->dismiss();
    QCOMPARE(c->phase(), QString("joining"));
}

// The reads the banner is made of answer for any room, joined or not.
void TestGroupCalls::occupantsRoundTripThroughRealTacky() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy results(&backend, &TackyBackend::result);
    QSignalSpy errors(&backend, &TackyBackend::error);
    GroupCall *c = m.callFor("me@example.com", "room@muc.example.com?join");
    QVERIFY(c);
    QTRY_VERIFY_WITH_TIMEOUT(results.count() + errors.count() >= 2, 5000);
    QVERIFY2(errors.isEmpty(),
             qPrintable(errors.isEmpty() ? "" : errors.first().at(1).toString()));
    QVERIFY(!c->active());
    QCOMPARE(c->count(), 0);
    QVERIFY(!c->joined());

    // Not in any call: the list is empty, and answers.
    results.clear();
    m.refreshFor("me@example.com");
    QTRY_VERIFY_WITH_TIMEOUT(results.count() >= 1, 5000);
    QVERIFY(errors.isEmpty());

    backend.stop();
}

// With nowhere to hold a call - here, an account that never connected, so no
// MUC service answers - starting one ends the join, saying why.
void TestGroupCalls::startWithNoServiceEnds() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@example.com", "bob@example.com");
    c->join(false);
    QCOMPARE(c->phase(), QString("joining"));
    QTRY_COMPARE_WITH_TIMEOUT(c->phase(), QString("ended"), 10000);
    QVERIFY2(!c->reason().isEmpty(), "no reason given");

    backend.stop();
}

QTEST_MAIN(TestGroupCalls)
#include "tst_groupcalls.moc"
