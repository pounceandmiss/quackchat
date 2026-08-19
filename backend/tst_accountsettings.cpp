#include <QtTest>
#include <QSignalSpy>

#include "AccountSettings.h"
#include "AvatarEncoder.h"
#include "TackyBackend.h"

namespace {
// Stands in for the QImage side, which lives in the GUI target. Hands back
// bytes that are not a PNG at all - nothing below here decodes them.
class StubEncoder : public AvatarEncoder {
public:
    QByteArray bytes = QByteArray("stub-avatar-bytes");
    QString failWith; // non-empty -> encode() reports this instead

    AvatarImage encode(const QUrl &source, QString *error) const override {
        ++calls;
        lastSource = source;
        if (!failWith.isEmpty()) {
            if (error)
                *error = failWith;
            return {};
        }
        return {bytes, 128, 128};
    }

    mutable int calls = 0;
    mutable QUrl lastSource;
};
} // namespace

class TestAccountSettings : public QObject {
    Q_OBJECT

private slots:
    void loadsFromCannedData();
    void nickChangedEventWithoutBackend();
    void devicesFollowTheAccount();
    void avatarNeedsAnEncoder();
    void avatarReportsAnUnreadablePicture();
    void ignoresProgressWhenNothingIsInFlight();
    void integrationLoadsAndSavesPassword();
    void integrationSaveWithoutChangesIsNoWrite();
    void integrationAvatarPublishFailureIsReported();
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
    s.handleEvent("conn", "State",
                  QVariantMap{{"acc", "other@h"}, {"state", "connected"}});
    QCOMPARE(sent.count(), 0); // not our account

    s.handleEvent("conn", "State",
                  QVariantMap{{"acc", "me@h"}, {"state", "connected"}});
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

// The headless build has no QImage side at all; saying so beats a crash.
void TestAccountSettings::avatarNeedsAnEncoder() {
    TackyBackend backend;
    QVERIFY(backend.start());
    AccountSettings s;
    s.setBackend(&backend);
    s.setAccount("me@example.com");

    s.setAvatar(QUrl::fromLocalFile("/tmp/whatever.png"));
    QVERIFY(!s.avatarBusy()); // nothing was sent, so nothing is pending
    QVERIFY(s.avatarError());
    QVERIFY(!s.avatarStatus().isEmpty());

    backend.stop();
}

// A picture that will not decode is refused before anything reaches the wire.
void TestAccountSettings::avatarReportsAnUnreadablePicture() {
    TackyBackend backend;
    QVERIFY(backend.start());
    StubEncoder encoder;
    encoder.failWith = "Unsupported image format";

    AccountSettings s;
    s.setAvatarEncoder(&encoder);
    s.setBackend(&backend);
    s.setAccount("me@example.com");

    s.setAvatar(QUrl::fromLocalFile("/tmp/notes.txt"));
    QCOMPARE(encoder.calls, 1);
    QCOMPARE(encoder.lastSource, QUrl::fromLocalFile("/tmp/notes.txt"));
    QVERIFY(!s.avatarBusy());
    QVERIFY(s.avatarError());
    QCOMPARE(s.avatarStatus(), QString("Unsupported image format"));

    backend.stop();
}

// `avatar <Progress>` is broadcast, so it also arrives for a publish some other
// page started. Showing one under a picture nobody is changing would be a line
// that never clears. (The line a publish of our own does show is in tst_qmlload,
// where there is a page to read it off.)
void TestAccountSettings::ignoresProgressWhenNothingIsInFlight() {
    AccountSettings s;
    s.setAccount("me@example.com");

    s.handleEvent("avatar", "Progress",
                  QVariantMap{{"acc", "me@example.com"},
                              {"message", "Uploading avatar data..."}});
    QCOMPARE(s.avatarStatus(), QString());
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

// A publish that cannot even be routed - here, to an account that was never
// added - is answered on the token rather than dropped: tacky catches the
// throw in `avatar publish` itself, and the JSON dispatcher catches whatever
// escapes before that. So the page reports the reason instead of sitting on
// "Publishing" for good, and needs no timer of its own.
//
// The other failure shape, a request that reaches the wire and is never
// answered, is covered by tacky's own 60s iq timeout - too long to wait for
// here, and not this side's behaviour to prove.
void TestAccountSettings::integrationAvatarPublishFailureIsReported() {
    TackyBackend backend;
    QVERIFY(backend.start()); // no account added, so routing has nothing to find

    StubEncoder encoder;
    AccountSettings s;
    s.setAvatarEncoder(&encoder);
    s.setBackend(&backend);
    s.setAccount("me@example.com");

    QSignalSpy busy(&s, &AccountSettings::avatarBusyChanged);
    s.setAvatar(QUrl::fromLocalFile("/tmp/whatever.png"));
    QCOMPARE(encoder.calls, 1);
    QVERIFY(s.avatarBusy());
    QCOMPARE(s.avatarStatus(), QString("Publishing"));

    QTRY_VERIFY_WITH_TIMEOUT(!s.avatarBusy(), 5000);
    QCOMPARE(busy.count(), 2); // in flight, then done
    QVERIFY(s.avatarError());
    QVERIFY(!s.avatarStatus().isEmpty());

    backend.stop();
}

QTEST_MAIN(TestAccountSettings)
#include "tst_accountsettings.moc"
