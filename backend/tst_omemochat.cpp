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
    void defaultsToEnabled();
    void followsTheEvent();
    void eventsAreScopedToAccountAndJid();
    void switchingChatsResetsToTheDefault();
    void groupchatIsUnavailable();
    void toggleFlipsBeforeTheEventLands();
    void readyReReadsTheSetting();
    void integrationPullAnswersWithTheStoredValue();
};

// Nothing has been asked yet and nothing may be assumed, so the composer starts
// where tacky does: encrypted.
void TestOmemoChat::defaultsToEnabled() {
    OmemoChat o;
    QVERIFY(o.enabled());
    chat(o);
    QVERIFY(o.enabled());
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

// The pull for a new chat is in flight when the old chat's answer is still on
// screen; showing an open padlock over a chat that encrypts (or the reverse) is
// the one thing this control must never do.
void TestOmemoChat::switchingChatsResetsToTheDefault() {
    OmemoChat o;
    chat(o);
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(!o.enabled());

    o.setJid("b@h");
    QVERIFY(o.enabled());
    // a@h's pull, answered after the switch, is about the chat we left.
    feedEvent(o, R"(["event","omemo","Enabled",
        {"acc":"me@h","jid":"a@h","value":false}])");
    QVERIFY(o.enabled());
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
    QVERIFY(!o.enabled());
    feedEvent(o, R"(["event","conn","Ready",{"acc":"me@h"}])");
    QVERIFY(o.enabled());
}

// A fixture cannot catch the pull being addressed wrongly - the event name
// travels bare and tacky puts the brackets back - so this leg goes through the
// real backend, with no server needed for a setting read.
void TestOmemoChat::integrationPullAnswersWithTheStoredValue() {
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
    QVERIFY(o.enabled());

    o.setEnabled(false);
    QTRY_VERIFY(!o.enabled());

    // Leave and come back: the answer has to be read again, not remembered.
    o.setJid("elsewhere@example.com");
    QVERIFY(o.enabled());
    o.setJid("peer@example.com");
    QTRY_VERIFY(!o.enabled());

    backend.stop();
}

QTEST_MAIN(TestOmemoChat)
#include "tst_omemochat.moc"
