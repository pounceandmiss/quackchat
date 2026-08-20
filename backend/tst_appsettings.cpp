// The app's own preferences off canned replies, plus a real libtacky round
// trip - the one thing that proves the `setting` argument names match the Tcl.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>

#include "AppSettings.h"
#include "TackyBackend.h"

static void feed(AppSettings &s, const QByteArray &json) {
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    s.handleEvent(arr.at(1).toString(), arr.at(2).toString(),
                  arr.at(3).toVariant());
}

class TestAppSettings : public QObject {
    Q_OBJECT
private slots:
    void unsetKeysReadAsTackysOwnDefaults();
    void changedEventsAreGlobal();
    void settingsRoundTripThroughTheBackend();
    void chatAvatarsIsOnUntilItIsTurnedOff();
    void refreshesWhenTheBackendConnects();
};

// Persisted values with no event to announce them, and on Android the link is
// still coming up when AppController first asks.
void TestAppSettings::refreshesWhenTheBackendConnects() {
    TackyBackend backend;
    AppSettings s;
    s.setBackend(&backend); // bound before the link is up
    QSignalSpy sent(&backend, &TackyBackend::sent);

    emit backend.connected();

    QStringList keys;
    for (const QList<QVariant> &call : sent) {
        QCOMPARE(call.at(0).toString(), QString("setting"));
        QCOMPARE(call.at(1).toString(), QString("get"));
        keys << call.at(2).toMap().value("key").toString();
    }
    keys.sort();
    QCOMPARE(keys, QStringList({"attachment_autofetch",
                                "attachment_autofetch_max", "chat_avatars",
                                "log_level", "log_native", "log_to_file"}));
}

// The store holds nothing until something is written, and "" is not a policy
// the backend has: unset, it is behaving as `everyone`.
void TestAppSettings::unsetKeysReadAsTackysOwnDefaults() {
    AppSettings s;
    QCOMPARE(s.attachmentAutofetch(), QString("everyone"));
    QCOMPARE(s.attachmentAutofetchMax(), 5242880LL);

    QSignalSpy changed(&s, &AppSettings::attachmentAutofetchChanged);
    s.handleResult(-1, QVariant(QString())); // no request of ours is in flight
    QCOMPARE(changed.count(), 0);

    // And an empty answer to one that was leaves the default standing.
    s.refresh(); // no backend, so no tokens are handed out
    feed(s, R"(["event","setting","Changed",
        {"key":"attachment_autofetch","value":""}])");
    QCOMPARE(s.attachmentAutofetch(), QString("everyone"));
    QCOMPARE(changed.count(), 0);
}

// The one preference here whose default is the set state, so an unwritten key
// and a stored "1" have to mean the same thing while "0" is the only way off.
void TestAppSettings::chatAvatarsIsOnUntilItIsTurnedOff() {
    AppSettings s;
    QVERIFY(s.chatAvatars());

    QSignalSpy changed(&s, &AppSettings::chatAvatarsChanged);
    // Answering the startup read with an unwritten key leaves it on, and says
    // nothing - the bindings were already showing what is in force.
    feed(s, R"(["event","setting","Changed",{"key":"chat_avatars","value":""}])");
    QVERIFY(s.chatAvatars());
    QCOMPARE(changed.count(), 0);

    feed(s, R"(["event","setting","Changed",{"key":"chat_avatars","value":"0"}])");
    QVERIFY(!s.chatAvatars());
    QCOMPARE(changed.count(), 1);

    // Stored on, which reads the same as never having been written.
    feed(s, R"(["event","setting","Changed",{"key":"chat_avatars","value":"1"}])");
    QVERIFY(s.chatAvatars());
    QCOMPARE(changed.count(), 2);
}

void TestAppSettings::changedEventsAreGlobal() {
    AppSettings s;
    QSignalSpy policy(&s, &AppSettings::attachmentAutofetchChanged);
    QSignalSpy cap(&s, &AppSettings::attachmentAutofetchMaxChanged);

    // No acc on the event: these belong to the app, not to an account.
    feed(s, R"(["event","setting","Changed",
        {"key":"attachment_autofetch","value":"contacts"}])");
    QCOMPARE(s.attachmentAutofetch(), QString("contacts"));
    QCOMPARE(policy.count(), 1);

    // Repeating a value must not churn the bindings behind the ticks.
    feed(s, R"(["event","setting","Changed",
        {"key":"attachment_autofetch","value":"contacts"}])");
    QCOMPARE(policy.count(), 1);

    // 0 is a real cap - no cap at all - not an unwritten key.
    feed(s, R"(["event","setting","Changed",
        {"key":"attachment_autofetch_max","value":"0"}])");
    QCOMPARE(s.attachmentAutofetchMax(), 0LL);
    QCOMPARE(cap.count(), 1);

    // A key this does not hold is somebody else's business.
    feed(s, R"(["event","setting","Changed",{"key":"chat_mode","value":"window"}])");
    QCOMPARE(policy.count(), 1);
    QCOMPARE(cap.count(), 1);
}

// Pins the key names and the get/set argument shape against the real module.
void TestAppSettings::settingsRoundTripThroughTheBackend() {
    TackyBackend backend;
    QVERIFY(backend.start());

    AppSettings s;
    s.setBackend(&backend);
    s.setAttachmentAutofetch(QStringLiteral("never"));
    s.setAttachmentAutofetchMax(1048576);
    s.setLogToFile(true);
    s.setLogLevel(QStringLiteral("debug"));
    s.setLogNative(true);
    s.setChatAvatars(false);
    // Shown straight away rather than after the round trip.
    QCOMPARE(s.attachmentAutofetch(), QString("never"));

    AppSettings readback;
    readback.setBackend(&backend);
    readback.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(
        readback.attachmentAutofetch() == QLatin1String("never"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(readback.attachmentAutofetchMax() == 1048576LL, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(readback.logToFile(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(readback.logLevel() == QLatin1String("debug"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(readback.logNative(), 5000);
    // Off is the value that has to travel: readback starts on, so this only
    // passes once the stored "0" has come back.
    QTRY_VERIFY_WITH_TIMEOUT(!readback.chatAvatars(), 5000);

    backend.stop();
}

QTEST_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
