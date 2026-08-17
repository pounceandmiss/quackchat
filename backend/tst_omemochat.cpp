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
    void groupchatIsUnavailable();
    void toggleFlipsBeforeTheEventLands();
    void readyReReadsTheSetting();
    void integrationReadAnswersWithTheStoredValue();
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

// A room has no OMEMO at all - tacky sends its messages in the clear whatever
// this says - so there is nothing to draw and nothing to set.
void TestOmemoChat::groupchatIsUnavailable() {
    OmemoChat o;
    chat(o, "room@h");
    o.setGroupchat(true);
    QVERIFY(!o.available());

    QSignalSpy spy(&o, &OmemoChat::enabledChanged);
    o.setEnabled(false);
    QVERIFY(o.enabled());
    QCOMPARE(spy.count(), 0);
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

    feedEvent(o, R"(["event","conn","Ready",{"acc":"other@h"}])");
    QVERIFY(o.known());
    feedEvent(o, R"(["event","conn","Ready",{"acc":"me@h"}])");
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

QTEST_MAIN(TestOmemoChat)
#include "tst_omemochat.moc"
