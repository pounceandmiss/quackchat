// GroupCall as a mirror of groupcall <State>, the re-seed after a reattach,
// and the wire shape against a real libtacky.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "GroupCall.h"
#include "GroupCallsModel.h"
#include "TackyBackend.h"

static void feed(GroupCallsModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QVariant role(GroupCallParticipants *p, int row, int r) {
    return p->data(p->index(row), r);
}

static QString peerState(GroupCallParticipants *p, int row) {
    return role(p, row, GroupCallParticipants::StateRole).toString();
}

static QString peerNick(GroupCallParticipants *p, int row) {
    return role(p, row, GroupCallParticipants::NickRole).toString();
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

// ann connected on video, and someone the room hides.
static const char *kLiveWithTwo = R"(["event","groupcall","State",
    {"acc":"me@host","jid":"k3j9@muc.host","chat":"bob@host","hosted":true,
     "phase":"live","reason":"","warning":"","since":1700000000000000,
     "video":true,"camera":true,"preview":{"name":"/run/tv-p"},"count":3,
     "participants":[
       {"nick":"ann","jid":"ann@host","joined":true,"video":true,
        "leg":"active","reason":"","warning":"","stream":"/run/tv-a"},
       {"nick":"zed","jid":"","joined":true,"video":false,
        "leg":"failed","reason":"the room hides their address","warning":"","stream":""}]}])";

class TestGroupCalls : public QObject {
    Q_OBJECT
private slots:
    void callForNormalisesTheRoom();
    void anIdleStateIsTheBanner();
    void startJoiningLiveEnded();
    void joinAnInRoomCallInProgress();
    void participantsFollowTheState();
    void videoAndPreviewFollowTheState();
    void leaveEndsHereAndStaleStatesDoNotUndoIt();
    void aFailedStartEndsTheJoin();
    void answerAndDeclineAStoredInvite();
    void anInviteRingsWithItsStoredMessage();
    void getOnConnectSeedsAndForgets();
    void dismissMakesRoomForANewJoin();

    void getRoundTripsThroughRealTacky();
    void startWithNoServiceEnds();
};

void TestGroupCalls::callForNormalisesTheRoom() {
    GroupCallsModel m;
    GroupCall *a = m.callFor("me@host", "Room@MUC.host?join");
    GroupCall *b = m.callFor("me@host", "room@muc.host/nick");
    QVERIFY(a);
    QCOMPARE(a, b);
    QCOMPARE(a->jid(), QString("room@muc.host"));
    QCOMPARE(a->phase(), QString("idle"));
    QVERIFY(!m.callFor("", "room@muc.host"));
}

// Out of the call there are no tiles.
void TestGroupCalls::anIdleStateIsTheBanner() {
    GroupCallsModel m;
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"room@muc.host","chat":"room@muc.host","hosted":false,
         "phase":"idle","count":2,"participants":[
           {"nick":"ann","jid":"ann@host","joined":true,"leg":"expected"},
           {"nick":"bob","jid":"bob@host","joined":true,"leg":"expected"}]}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    QVERIFY(c);
    QVERIFY(c->active());
    QCOMPARE(c->count(), 2);
    QVERIFY(!c->joined());
    QCOMPARE(c->phase(), QString("idle"));
    QCOMPARE(c->participants()->rowCount(), 0);

    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"room@muc.host","chat":"room@muc.host",
         "phase":"idle","count":0,"participants":[]}])");
    QVERIFY(!c->active());
    QCOMPARE(c->count(), 0);
}

void TestGroupCalls::startJoiningLiveEnded() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    GroupCall *c = m.callFor("me@host", "bob@host");
    c->join(true);
    QCOMPARE(c->phase(), QString("joining"));
    const QVariantMap start = lastSent(sent, "groupcall", "start");
    QCOMPARE(start.value("chat").toString(), QString("bob@host"));
    QCOMPARE(start.value("video").toInt(), 1);

    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"bob@host","hosted":true,
         "phase":"joining","video":true,"camera":true,"preview":{},"count":0,
         "participants":[]}])");
    QCOMPARE(c->callJid(), QString("k3j9@muc.host"));
    QCOMPARE(c->phase(), QString("joining"));

    feed(m, kLiveWithTwo);
    QCOMPARE(c->phase(), QString("live"));
    QVERIFY(c->inCall());
    QVERIFY(c->joined());
    QCOMPARE(c->count(), 3);
    QCOMPARE(c->startedAt(), qint64(1700000000000));
    QCOMPARE(c->participants()->rowCount(), 2);

    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"bob@host","hosted":true,
         "phase":"ended","reason":"disconnected","count":0,"participants":[]}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString("disconnected"));
    QVERIFY(!c->sendingVideo());
    // The tiles stay, greyed.
    QCOMPARE(c->participants()->rowCount(), 2);
    QCOMPARE(peerState(c->participants(), 0), QString("ended"));
    QCOMPARE(peerState(c->participants(), 1), QString("failed"));
    QVERIFY(!role(c->participants(), 0, GroupCallParticipants::HasVideoRole).toBool());
}

void TestGroupCalls::joinAnInRoomCallInProgress() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"room@muc.host","chat":"room@muc.host",
         "phase":"idle","count":1,"participants":[
           {"nick":"ann","jid":"ann@host","joined":true,"leg":"expected"}]}])");
    GroupCall *c = m.find("me@host", "room@muc.host");
    c->join(false);
    const QVariantMap join = lastSent(sent, "groupcall", "join");
    QCOMPARE(join.value("jid").toString(), QString("room@muc.host"));
    QVERIFY(lastSent(sent, "groupcall", "start").isEmpty());
}

void TestGroupCalls::participantsFollowTheState() {
    GroupCallsModel m;
    feed(m, kLiveWithTwo);
    GroupCall *c = m.find("me@host", "bob@host");
    GroupCallParticipants *p = c->participants();
    QCOMPARE(peerNick(p, 0), QString("ann"));
    QCOMPARE(peerState(p, 0), QString("active"));
    QCOMPARE(role(p, 0, GroupCallParticipants::JidRole).toString(), QString("ann@host"));
    QCOMPARE(peerNick(p, 1), QString("zed"));
    QCOMPARE(role(p, 1, GroupCallParticipants::ReasonRole).toString(),
             QString("the room hides their address"));

    // ann leaves, carol arrives: zed's row keeps its place.
    QSignalSpy removed(p, &QAbstractItemModel::rowsRemoved);
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"bob@host","phase":"live",
         "since":1700000000000000,"count":3,"participants":[
           {"nick":"zed","jid":"","joined":true,"leg":"failed","reason":"the room hides their address"},
           {"nick":"carol","jid":"carol@host","joined":false,"leg":"expected"}]}])");
    QCOMPARE(removed.count(), 1);
    QCOMPARE(p->rowCount(), 2);
    QCOMPARE(peerNick(p, 0), QString("zed"));
    QCOMPARE(peerNick(p, 1), QString("carol"));
    QCOMPARE(peerState(p, 1), QString("expected"));
}

void TestGroupCalls::videoAndPreviewFollowTheState() {
    GroupCallsModel m;
    feed(m, kLiveWithTwo);
    GroupCall *c = m.find("me@host", "bob@host");
    GroupCallParticipants *p = c->participants();
    QVERIFY(role(p, 0, GroupCallParticipants::HasVideoRole).toBool());
    QCOMPARE(role(p, 0, GroupCallParticipants::RemoteVideoRole).toMap().value("name").toString(),
             QString("/run/tv-a"));
    QVERIFY(!role(p, 1, GroupCallParticipants::HasVideoRole).toBool());
    QVERIFY(c->sendingVideo());
    QCOMPARE(c->preview().value("name").toString(), QString("/run/tv-p"));
    QVERIFY(c->video());
    QVERIFY(c->cameraOn());
}

// A state sent before the backend heard of our leave does not undo it.
void TestGroupCalls::leaveEndsHereAndStaleStatesDoNotUndoIt() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    feed(m, kLiveWithTwo);
    GroupCall *c = m.find("me@host", "bob@host");
    c->leave();
    QCOMPARE(lastSent(sent, "groupcall", "leave").value("jid").toString(),
             QString("k3j9@muc.host"));
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString());

    feed(m, kLiveWithTwo);
    QCOMPARE(c->phase(), QString("ended"));
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"bob@host",
         "phase":"ended","reason":"","count":0,"participants":[]}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString());
}

void TestGroupCalls::aFailedStartEndsTheJoin() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    GroupCall *c = m.callFor("me@host", "bob@host");
    c->join(false);
    feed(m, R"(["event","groupcall","StartFailed",
        {"acc":"me@host","chat":"bob@host","reason":"no group chat service to hold the call"}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString("no group chat service to hold the call"));
}

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
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"k3j9@muc.host","chat":"room@muc.host","hosted":true,
         "phase":"ended","reason":"the call has ended","count":0,"participants":[]}])");
    QCOMPARE(c->phase(), QString("ended"));
    QCOMPARE(c->reason(), QString("the call has ended"));

    c->declineInvite("room@muc.host?join", 43);
    const QVariantMap no = lastSent(sent, "groupcall", "decline");
    QCOMPARE(no.value("chat").toString(), QString("room@muc.host?join"));
    QCOMPARE(no.value("timestamp").toLongLong(), 43);
}

void TestGroupCalls::anInviteRingsWithItsStoredMessage() {
    GroupCallsModel m;
    QSignalSpy rings(&m, &GroupCallsModel::invited);
    feed(m, R"(["event","groupcall","Invited",
        {"acc":"me@host","jid":"k3j9@muc.host","from":"bob@host","chat":"bob@host",
         "timestamp":42,"video":true}])");
    QCOMPARE(rings.count(), 1);
    QCOMPARE(rings.at(0).at(1).toString(), QString("bob@host"));
    QCOMPARE(rings.at(0).at(2).toLongLong(), 42);
    QCOMPARE(rings.at(0).at(3).toString(), QString("k3j9@muc.host"));
    QVERIFY(rings.at(0).at(5).toBool());
    QVERIFY(m.find("me@host", "bob@host"));
}

// A call the answer leaves out ended while nobody was looking.
void TestGroupCalls::getOnConnectSeedsAndForgets() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    feed(m, R"(["event","groupcall","State",
        {"acc":"me@host","jid":"old@muc.host","chat":"carol@host","phase":"live",
         "count":2,"participants":[]}])");
    GroupCall *old = m.find("me@host", "carol@host");
    QCOMPARE(old->phase(), QString("live"));

    feed(m, R"(["event","conn","State",{"acc":"me@host","state":"connected"}])");
    QVERIFY(!lastSent(sent, "groupcall", "get").isEmpty());
    const QJsonArray live = QJsonDocument::fromJson(kLiveWithTwo).array();
    m.handleResult(1, QVariantList{live.at(3).toVariant()});
    GroupCall *c = m.find("me@host", "bob@host");
    QVERIFY(c);
    QCOMPARE(c->phase(), QString("live"));
    QCOMPARE(c->participants()->rowCount(), 2);
    QCOMPARE(old->phase(), QString("ended"));
}

void TestGroupCalls::dismissMakesRoomForANewJoin() {
    TackyBackend backend;
    GroupCallsModel m;
    m.setBackend(&backend);
    feed(m, kLiveWithTwo);
    GroupCall *c = m.find("me@host", "bob@host");
    c->dismiss(); // a live call is not dismissed
    QCOMPARE(c->phase(), QString("live"));
    c->leave();
    c->dismiss();
    QCOMPARE(c->phase(), QString("idle"));
    QCOMPARE(c->participants()->rowCount(), 0);
    c->join(false);
    QCOMPARE(c->phase(), QString("joining"));
}

void TestGroupCalls::getRoundTripsThroughRealTacky() {
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
    const int get = backend.request("account", "list") + 1; // refreshFor's ask
    m.refreshFor("me@example.com");
    const auto answer = [&]() -> QVariant {
        for (const QList<QVariant> &r : results)
            if (r.at(0).toInt() == get)
                return r.at(1);
        return {};
    };
    QTRY_VERIFY_WITH_TIMEOUT(answer().isValid(), 5000);
    QVERIFY2(errors.isEmpty(),
             qPrintable(errors.isEmpty() ? "" : errors.first().at(1).toString()));
    QCOMPARE(answer().toList().size(), 0);
    QVERIFY(!c->active());
    QCOMPARE(c->phase(), QString("idle"));

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
