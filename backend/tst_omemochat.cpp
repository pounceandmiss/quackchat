#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>

#include "OmemoChat.h"
#include "TackyBackend.h"

static void feedEvent(OmemoChat &o, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    o.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

class TestOmemoChat : public QObject {
    Q_OBJECT

    // The ordinary case: an open 1:1 on a live account.
    static void chat(OmemoChat &o, const QString &jid = "a@h") {
        o.setAccount("me@h");
        o.setJid(jid);
    }

private slots:
    void saysNothingUntilItHasBeenTold();
    void readsWithATokenSoAFailureIsVisible();
    void theAnswerSettlesIt();
    void aFailedReadLeavesItUnknown();
    void reReadsOnTheConnectedEdge();
    void followsTheEvent();
    void eventsAreScopedToAccountAndJid();
    void switchingChatsGoesBackToUnknown();
    void aRoomIsReadForItsStatus();
    void aRoomIsUnavailableUntilItQualifies();
    void aRoomThatStoppedQualifyingKeepsItsSwitch();
    void aRefusedSwitchReadsTheRoomAgain();
    void everyStoppedSendIsShown();
    void toggleFlipsBeforeTheEventLands();
    void readyReReadsTheSetting();
    void integrationReadAnswersWithTheStoredValue();
    void integrationARoomIsAnsweredForBeforeItIsKnown();
};

// The default is on, which over a chat that is really off would draw a padlock
// on a conversation going out in the clear. So until the read answers there is
// nothing to draw, and `enabled` is a placeholder rather than a state.
void TestOmemoChat::saysNothingUntilItHasBeenTold() {
    OmemoChat o;
    QVERIFY(!o.known());
    chat(o);
    QVERIFY(!o.known());
}

void TestOmemoChat::readsWithATokenSoAFailureIsVisible() {
    TackyBackend backend; // never started: `sent` still reports what was asked
    OmemoChat o;
    o.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    chat(o);

    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("omemo"));
    QCOMPARE(sent.first().at(1).toString(), QString("isEnabled"));
    QCOMPARE(sent.first().at(2).toMap().value("acc").toString(), QString("me@h"));
    QCOMPARE(sent.first().at(2).toMap().value("jid").toString(), QString("a@h"));
}

void TestOmemoChat::theAnswerSettlesIt() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    QSignalSpy spy(&o, &OmemoChat::knownChanged);
    chat(o); // token 1

    o.handleResult(1, false);
    QVERIFY(o.known());
    QVERIFY(!o.enabled());
    QCOMPARE(spy.count(), 1);
}

// A read that went out over a dead link answers with an error. It stays unknown
// rather than falling back to the default, and the next connected edge asks
// again.
void TestOmemoChat::aFailedReadLeavesItUnknown() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    chat(o); // token 1

    o.handleError(1, QStringLiteral("omemo/isEnabled was not sent: no backend"));
    QVERIFY(!o.known());
    // And a late answer on the spent token cannot settle it either.
    o.handleResult(1, true);
    QVERIFY(!o.known());
}

// On Android the link comes up after the first chat is already on screen, so
// the read that went out then reached nothing.
void TestOmemoChat::reReadsOnTheConnectedEdge() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    chat(o);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    emit backend.connected();

    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(1).toString(), QString("isEnabled"));
}

void TestOmemoChat::followsTheEvent() {
    OmemoChat o;
    chat(o);
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(!o.enabled());
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":true}])");
    QVERIFY(o.enabled());
}

// Every open chat on every account sees the same event stream, and this one
// decides whether the next message is readable by anyone who catches it.
void TestOmemoChat::eventsAreScopedToAccountAndJid() {
    OmemoChat o;
    chat(o);
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"other@h","jid":"a@h","value":false}])");
    QVERIFY(o.enabled());
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"b@h","value":false}])");
    QVERIFY(o.enabled());
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(!o.enabled());
}

// The read for a new chat is in flight while the old chat's answer is still on
// screen; showing an open padlock over a chat that encrypts (or the reverse) is
// the one thing this control must never do.
void TestOmemoChat::switchingChatsGoesBackToUnknown() {
    OmemoChat o;
    chat(o);
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(o.known());
    QVERIFY(!o.enabled());

    o.setJid("b@h");
    QVERIFY(!o.known());
    // a@h's answer, arriving after the switch, is about the chat we left.
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(!o.known());
}

static void room(OmemoChat &o) {
    o.setGroupchat(true);
    o.setAccount("me@h");
    o.setJid("room@muc.h?join");
}

static QVariantMap roomStatus(bool eligible, bool enabled,
                              const QStringList &reasons = {}) {
    return QVariantMap{{"jid", "room@muc.h?join"},
                       {"eligible", eligible},
                       {"enabled", enabled},
                       // tacky's word for whether there is a switch to show
                       {"offered", eligible || enabled},
                       {"reasons", reasons},
                       {"member_list", eligible ? "complete" : "none"},
                       {"members", QVariantList{}},
                       {"attention", 0},
                       {"unreachable", QVariantList{}}};
}

// One read says both whether the room is on and whether it could be.
void TestOmemoChat::aRoomIsReadForItsStatus() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    room(o);

    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.last().at(1).toString(), QString("roomStatus"));
    QCOMPARE(sent.last().at(2).toMap().value("jid").toString(),
             QString("room@muc.h?join"));
}

void TestOmemoChat::aRoomIsUnavailableUntilItQualifies() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    room(o); // token 1
    QVERIFY(!o.available());

    o.handleResult(1, roomStatus(false, false, {"anonymous"}));
    QVERIFY(o.known());
    QVERIFY(!o.available());
    QCOMPARE(o.reasons(), QStringList{"anonymous"});
    o.setEnabled(true);
    QVERIFY(!o.enabled());

    QSignalSpy available(&o, &OmemoChat::availableChanged);
    o.handleEvent("omemo", "RoomStatus",
                  QVariantMap{{"acc", "me@h"}, {"jid", "room@muc.h?join"},
                              {"status", roomStatus(true, false)}});
    QVERIFY(o.available());
    QCOMPARE(available.count(), 1);
    QVERIFY(o.reasons().isEmpty());
}

// On, then the room went public: its sends fail rather than go out in the
// clear, and the way out of that is the switch.
void TestOmemoChat::aRoomThatStoppedQualifyingKeepsItsSwitch() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    room(o);
    o.handleResult(1, roomStatus(false, true, {"not_members_only"}));
    QVERIFY(o.available());
    QVERIFY(o.enabled());

    o.setEnabled(false);
    QVERIFY(!o.enabled());
    // Off, it has no switch left - once tacky says so.
    o.handleEvent("omemo", "RoomStatus",
                  QVariantMap{{"acc", "me@h"}, {"jid", "room@muc.h?join"},
                              {"status", roomStatus(false, false, {"not_members_only"})}});
    QVERIFY(!o.available());
    o.setEnabled(true);
    QVERIFY(!o.enabled());
}

// The room stopped qualifying between drawing the switch and the click, and
// tacky refused it. What is on screen goes back to what the room says.
void TestOmemoChat::aRefusedSwitchReadsTheRoomAgain() {
    TackyBackend backend;
    OmemoChat o;
    o.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    room(o);
    o.handleResult(1, roomStatus(true, false));

    o.setEnabled(true); // token 2
    QVERIFY(o.enabled());
    QCOMPARE(sent.last().at(1).toString(), QString("setEnabled"));

    o.handleError(2, "OMEMO ROOM_NOT_ELIGIBLE");
    QVERIFY(!o.known());
    QCOMPARE(sent.last().at(1).toString(), QString("roomStatus"));
}

// <RoomStatus> only says what changed, so the same members stopping a second
// send arrive as <MembersUnreachable> alone - after the first was put away.
void TestOmemoChat::everyStoppedSendIsShown() {
    OmemoChat o;
    room(o);
    const QVariantMap stopped{
        {"acc", "me@h"}, {"jid", "room@muc.h?join"},
        {"members", QVariantList{QVariantMap{{"jid", "bob@h"}, {"reason", "no_devices"}}}}};

    o.handleEvent("omemo", "MembersUnreachable", stopped);
    QCOMPARE(o.unreachable().size(), 1);
    o.dismissUnreachable();
    QVERIFY(o.unreachable().isEmpty());
    o.handleEvent("omemo", "MembersUnreachable", stopped);
    QCOMPARE(o.unreachable().size(), 1);

    // Another room's is not this one's.
    o.dismissUnreachable();
    QVariantMap elsewhere = stopped;
    elsewhere["jid"] = "other@muc.h?join";
    o.handleEvent("omemo", "MembersUnreachable", elsewhere);
    QVERIFY(o.unreachable().isEmpty());
}

void TestOmemoChat::toggleFlipsBeforeTheEventLands() {
    OmemoChat o;
    chat(o);
    QSignalSpy spy(&o, &OmemoChat::enabledChanged);
    o.setEnabled(false);
    QVERIFY(!o.enabled());
    QCOMPARE(spy.count(), 1);
}

// The account's own store, where the setting lives, is only opened once the
// account connects - so an answer from before that was an answer from nothing.
void TestOmemoChat::readyReReadsTheSetting() {
    OmemoChat o;
    chat(o);
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(!o.enabled());

    feedEvent(o, R"(["event","conn","State",{"acc":"other@h","state":"connected"}])");
    QVERIFY(o.known());
    feedEvent(o, R"(["event","conn","State",{"acc":"me@h","state":"connected"}])");
    QVERIFY(!o.known());
}

// A fixture cannot catch the read being addressed wrongly, or a schema entry
// that would hand a bool back as a string, so this leg goes through the real
// backend. No server is needed for a setting read.
void TestOmemoChat::integrationReadAnswersWithTheStoredValue() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    OmemoChat o;
    o.setBackend(&backend);
    o.setAccount("me@example.com");
    o.setJid("peer@example.com");
    // A chat nobody has set reads back on, from the backend rather than here.
    QTRY_VERIFY(o.known());
    QVERIFY(o.enabled());

    o.setEnabled(false);
    QTRY_VERIFY(!o.enabled());

    // Leave and come back: the answer has to be read again, not remembered.
    o.setJid("elsewhere@example.com");
    QVERIFY(!o.known());
    o.setJid("peer@example.com");
    QTRY_VERIFY(o.known());
    QVERIFY(!o.enabled());

    backend.stop();
}

// A room nothing is known about yet: the status is a full answer, schema and
// all, saying it does not qualify and why.
void TestOmemoChat::integrationARoomIsAnsweredForBeforeItIsKnown() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    OmemoChat o;
    o.setBackend(&backend);
    o.setGroupchat(true);
    o.setAccount("me@example.com");
    o.setJid("room@muc.example.com?join");
    QTRY_VERIFY(o.known());
    QVERIFY(!o.enabled());
    QVERIFY(!o.eligible());
    QVERIFY(!o.available());
    QCOMPARE(o.reasons(), QStringList{"unknown"});

    backend.stop();
}

QTEST_MAIN(TestOmemoChat)
#include "tst_omemochat.moc"
