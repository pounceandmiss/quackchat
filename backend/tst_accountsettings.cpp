#include <QtTest>
#include <QSignalSpy>

#include "AccountSettings.h"
#include "TackyBackend.h"

class TestAccountSettings : public QObject {
    Q_OBJECT

private slots:
    void loadsFromCannedData();
    void nickChangedEventWithoutBackend();
    void devicesFollowTheAccount();
    void integrationLoadsAndSavesPassword();
    void integrationSaveWithoutChangesIsNoWrite();
    void refetchesNickOnReady();
};

// The nick is server state, so a fresh session can carry a different one than
// the reply we got against the old session.
void TestAccountSettings::refetchesNickOnReady() {
    TackyBackend backend;
    AccountSettings s;
    s.setBackend(&backend);
    s.setAccount("me@h");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    s.handleEvent("conn", "Ready", QVariantMap{{"acc", "other@h"}});
    QCOMPARE(sent.count(), 0); // not our account

    s.handleEvent("conn", "Ready", QVariantMap{{"acc", "me@h"}});
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().at(0).toString(), QString("nick"));
}

void TestAccountSettings::loadsFromCannedData() {
    AccountSettings s;
    QSignalSpy pw(&s, &AccountSettings::passwordChanged);
    QSignalSpy nick(&s, &AccountSettings::nickChanged);

    s.applyAccount(QVariantMap{{"jid", "me@h"}, {"password", "hunter2"},
                               {"enabled", true}});
    QCOMPARE(s.password(), QString("hunter2"));
    QCOMPARE(pw.count(), 1);

    s.applyAccount(QVariantMap{{"password", "hunter2"}}); // unchanged
    QCOMPARE(pw.count(), 1);

    s.applyNick("Kitsunia");
    QCOMPARE(s.nick(), QString("Kitsunia"));
    QCOMPARE(nick.count(), 1);
}

// Events arrive whether or not this instance ever got a backend.
void TestAccountSettings::nickChangedEventWithoutBackend() {
    AccountSettings s;
    s.setAccount("me@h");
    s.handleEvent("nick", "Changed", QVariantMap{{"acc", "me@h"}, {"jid", "me@h"}});
    QCOMPARE(s.nick(), QString());
}

void TestAccountSettings::devicesFollowTheAccount() {
    AccountSettings s;
    s.setAccount("me@h");
    QVERIFY(s.devices());
    QCOMPARE(s.devices()->account(), QString("me@h"));
    // The page shows the account's own keys, so it is its own subject.
    QCOMPARE(s.devices()->jid(), QString("me@h"));
}

// Real backend: the stored credential round-trips through `account get` and
// `account add`, which is how tacky's own sign-in form writes it.
void TestAccountSettings::integrationLoadsAndSavesPassword() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "old"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    AccountSettings s;
    s.setBackend(&backend);
    s.setAccount("me@example.com");
    QTRY_COMPARE_WITH_TIMEOUT(s.password(), QString("old"), 5000);

    QSignalSpy saved(&s, &AccountSettings::saved);
    s.save("new", s.nick());
    QCOMPARE(saved.count(), 1); // nothing to wait on but the local write
    QCOMPARE(s.password(), QString("new"));
    QCOMPARE(s.status(), QString("Saved"));

    // Re-read it from the backend rather than trust the in-memory copy.
    s.applyAccount({});
    s.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(s.password(), QString("new"), 5000);

    backend.stop();
}

void TestAccountSettings::integrationSaveWithoutChangesIsNoWrite() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "old"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    AccountSettings s;
    s.setBackend(&backend);
    s.setAccount("me@example.com");
    QTRY_COMPARE_WITH_TIMEOUT(s.password(), QString("old"), 5000);

    QSignalSpy saved(&s, &AccountSettings::saved);
    s.save(s.password(), s.nick());
    QCOMPARE(saved.count(), 1);
    QCOMPARE(s.status(), QString()); // nothing was written, so nothing to report
    QVERIFY(!s.saving());

    backend.stop();
}

QTEST_MAIN(TestAccountSettings)
#include "tst_accountsettings.moc"
