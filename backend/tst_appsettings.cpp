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
};

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
    // Shown straight away rather than after the round trip.
    QCOMPARE(s.attachmentAutofetch(), QString("never"));

    AppSettings readback;
    readback.setBackend(&backend);
    readback.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(
        readback.attachmentAutofetch() == QLatin1String("never"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(readback.attachmentAutofetchMax() == 1048576LL, 5000);

    backend.stop();
}

QTEST_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
