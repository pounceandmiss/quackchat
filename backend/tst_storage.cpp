// Local storage encryption off canned replies, plus a real libtacky round trip
// through an encrypt/relaunch/unlock cycle - the one thing that proves the
// `storage` argument names match the Tcl and that the SQLCipher archive works.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "StorageController.h"
#include "TackyBackend.h"

namespace {

void feed(StorageController &s, const QByteArray &json) {
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    s.handleEvent(arr.at(1).toString(), arr.at(2).toString(),
                  arr.at(3).toVariant());
}

// The token of the last frame the controller sent, so a reply can be aimed at
// it without the controller exposing its bookkeeping. Every frame it sends is a
// request - it has no fire-and-forget calls - and TackyBackend numbers those
// from 1, so the count is the token.
int lastToken(const QSignalSpy &sent) {
    return sent.isEmpty() ? -1 : sent.size();
}

QStringList methodsAsked(const QSignalSpy &sent) {
    QStringList out;
    for (const QList<QVariant> &call : sent) {
        if (call.at(0).toString() == QLatin1String("storage"))
            out << call.at(1).toString();
    }
    return out;
}

// Replies from a real backend, matched by token. Empty until one lands.
QVariant resultFor(const QSignalSpy &results, int token) {
    for (const QList<QVariant> &r : results)
        if (r.at(0).toInt() == token)
            return r.at(1);
    return QVariant();
}

QString errorFor(const QSignalSpy &errors, int token) {
    for (const QList<QVariant> &e : errors)
        if (e.at(0).toInt() == token)
            return e.at(1).toString();
    return QString();
}

const QLatin1String kPass("a passphrase with a ' in it");

} // namespace

class TestStorage : public QObject {
    Q_OBJECT
private slots:
    void statusIsUnknownUntilItIsAnswered();
    void onlyLockedAndPendingGate();
    void aMigrationRequestedFromInsideTheAppDoesNotGate();
    void unlockingIntoAPendingDecryptKeepsTheGate();
    void aFailedActionKeepsTheStatusAndSaysWhy();
    void aFailedStatusLeavesTheAppWaiting();
    void migrateProgressMovesTheCounters();
    void everyActionRereadsTheStatusRatherThanAssumingIt();
    void anUnlockedEventRereadsTheStatus();
    void refreshesWhenTheBackendConnects();
    void passphraseRidesUnderTheNameTheTclReads();
    void encryptRoundTripsThroughARelaunch();
    void cancellingAtTheGateStillOpensTheApp();
};

// "" is not "not encrypted": acting on it would let the app boot as though the
// store were plaintext when the answer is simply not in yet.
void TestStorage::statusIsUnknownUntilItIsAnswered() {
    StorageController s;
    QCOMPARE(s.status(), QString());
    QVERIFY(!s.gateActive());
    QVERIFY(!s.ready());
}

void TestStorage::onlyLockedAndPendingGate() {
    struct Case {
        const char *status;
        bool gates;
    };
    // A pending migration gates as hard as a locked store: tacky installs no
    // other module until it has run.
    const Case cases[] = {{"plaintext", false},
                          {"unlocked", false},
                          {"locked", true},
                          {"pending-encrypt", true},
                          {"pending-decrypt", true}};
    // One controller each: the question is what the app does when it *starts*
    // in this state, and the gate is armed once, at the first answer.
    for (const Case &c : cases) {
        TackyBackend backend;
        StorageController s;
        s.setBackend(&backend);
        QSignalSpy sent(&backend, &TackyBackend::sent);

        s.refresh();
        s.handleResult(lastToken(sent), QString::fromLatin1(c.status));
        QCOMPARE(s.status(), QString::fromLatin1(c.status));
        QCOMPARE(s.gateActive(), c.gates);
        QCOMPARE(s.ready(), !c.gates);
    }
}

// Encrypt/decrypt only ever run at the pre-boot gate, before any account has
// connected. Requesting one from Preferences must therefore not raise the gate:
// the request is for the next launch, and honouring it here would migrate with
// every account live. The status is the same string either way, so only *when*
// it arrived tells them apart.
void TestStorage::aMigrationRequestedFromInsideTheAppDoesNotGate() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.refresh();
    s.handleResult(lastToken(sent), QStringLiteral("plaintext"));
    QVERIFY(!s.gateActive());
    QVERIFY(s.ready());

    // What Preferences does: ask, then re-read.
    s.requestEncrypt();
    s.handleResult(lastToken(sent), QVariant());
    s.handleResult(lastToken(sent), QStringLiteral("pending-encrypt"));

    QCOMPARE(s.status(), QString("pending-encrypt"));
    QVERIFY2(!s.gateActive(),
             "asking to encrypt put the passphrase prompt up mid-session");
    // And the app stays open for business while the request sits there.
    QVERIFY(s.ready());

    // Taking it back changes nothing about either.
    s.cancelPending();
    s.handleResult(lastToken(sent), QVariant());
    s.handleResult(lastToken(sent), QStringLiteral("plaintext"));
    QVERIFY(!s.gateActive());
    QVERIFY(s.ready());
}

// The one chain where a gating status follows another: unlocking is what
// reveals a decrypt request, and it still has to run before the app opens. So
// the gate has to survive that hop rather than disarming on the way through.
void TestStorage::unlockingIntoAPendingDecryptKeepsTheGate() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.refresh();
    s.handleResult(lastToken(sent), QStringLiteral("locked"));
    QVERIFY(s.gateActive());

    s.unlock(kPass);
    s.handleResult(lastToken(sent), QVariant());
    s.handleResult(lastToken(sent), QStringLiteral("pending-decrypt"));
    QVERIFY2(s.gateActive(), "the revealed decrypt request lost its gate");
    QVERIFY(!s.ready());

    // Running it is what finally opens the app.
    s.decrypt();
    s.handleResult(lastToken(sent), QVariant());
    s.handleResult(lastToken(sent), QStringLiteral("plaintext"));
    QVERIFY(!s.gateActive());
    QVERIFY(s.ready());
}

// A wrong passphrase is retried in place, so the gate has to stay exactly where
// it was - same status, no longer busy, with tacky's own wording to show.
void TestStorage::aFailedActionKeepsTheStatusAndSaysWhy() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.refresh();
    s.handleResult(lastToken(sent), QStringLiteral("locked"));

    s.unlock(QStringLiteral("wrong"));
    QVERIFY(s.busy());
    s.handleError(lastToken(sent), QStringLiteral("incorrect passphrase"));

    QVERIFY(!s.busy());
    QCOMPARE(s.error(), QString("incorrect passphrase"));
    QCOMPARE(s.status(), QString("locked"));
    QVERIFY(s.gateActive());

    // And the next attempt clears it, so a stale message never sits under a
    // retry that is still running.
    s.unlock(QStringLiteral("right"));
    QCOMPARE(s.error(), QString());
}

// The status query itself failing is not an answer. Reporting it as plaintext
// would boot the app over an encrypted store; reporting it as locked would
// gate a plaintext one forever. So it stays unknown and the app waits.
void TestStorage::aFailedStatusLeavesTheAppWaiting() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.refresh();
    s.handleError(lastToken(sent), QStringLiteral("backend stopped"));

    QCOMPARE(s.status(), QString());
    QVERIFY(!s.ready());
    QVERIFY(!s.gateActive());
    QCOMPARE(s.error(), QString());
}

void TestStorage::migrateProgressMovesTheCounters() {
    StorageController s;
    QSignalSpy progress(&s, &StorageController::progressChanged);

    feed(s, R"(["event","storage","MigrateProgress",{"done":3,"total":11}])");
    QCOMPARE(s.progressDone(), 3);
    QCOMPARE(s.progressTotal(), 11);
    QVERIFY(progress.count() > 0);
}

// Unlocking a store that also has a decrypt pending lands on pending-decrypt,
// not unlocked - so the next state is read, never assumed. tacky's own Tk
// client re-checks for exactly this reason.
void TestStorage::everyActionRereadsTheStatusRatherThanAssumingIt() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    // Not cleared between steps: the spy's length is what names the token.
    s.refresh();
    s.handleResult(lastToken(sent), QStringLiteral("locked"));

    s.unlock(kPass);
    s.handleResult(lastToken(sent), QVariant());
    QCOMPARE(methodsAsked(sent), QStringList({"status", "unlock", "status"}));

    // And that second answer is what puts the second gate up.
    s.handleResult(lastToken(sent), QStringLiteral("pending-decrypt"));
    QVERIFY(s.gateActive());
    QVERIFY(!s.busy());
}

// On Android the interpreter lives in a service that outlives the UI, so
// another view of it can be what answered the passphrase.
void TestStorage::anUnlockedEventRereadsTheStatus() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    feed(s, R"(["event","storage","Unlocked",{}])");
    QCOMPARE(methodsAsked(sent), QStringList({"status"}));

    sent.clear();
    feed(s, R"(["event","storage","MigrationComplete",{"direction":"encrypt"}])");
    QCOMPARE(methodsAsked(sent), QStringList({"status"}));
}

// Nothing announces the status, and on Android the socket is still coming up
// when AppController first asks.
void TestStorage::refreshesWhenTheBackendConnects() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend); // bound before the link is up
    QSignalSpy sent(&backend, &TackyBackend::sent);

    emit backend.connected();

    QCOMPARE(methodsAsked(sent), QStringList({"status"}));
}

// The Tcl reads -passphrase, and PRAGMA key takes it as a SQL literal, so a
// quote in it has to survive the trip. Checked on the frame rather than through
// the backend: the round trip below proves the other half.
void TestStorage::passphraseRidesUnderTheNameTheTclReads() {
    TackyBackend backend;
    StorageController s;
    s.setBackend(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.unlock(kPass);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).at(0).toString(), QString("storage"));
    QCOMPARE(sent.at(0).at(1).toString(), QString("unlock"));
    QCOMPARE(sent.at(0).at(2).toMap().value("passphrase").toString(),
             QString(kPass));

    sent.clear();
    s.encrypt(kPass);
    QCOMPARE(sent.at(0).at(1).toString(), QString("encrypt"));
    QCOMPARE(sent.at(0).at(2).toMap().value("passphrase").toString(),
             QString(kPass));
}

// The whole two-step against the real module: request, relaunch, migrate at the
// gate, then relaunch again and unlock. An account written while plaintext has
// to still be there at the end, which is the only proof the migration carried
// the data rather than just the file.
void TestStorage::encryptRoundTripsThroughARelaunch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList args{QStringLiteral("-transient"), QStringLiteral("0"),
                           QStringLiteral("-config-dir"), dir.filePath("cfg"),
                           QStringLiteral("-data-dir"), dir.filePath("data"),
                           QStringLiteral("-cache-dir"), dir.filePath("cache")};
    const QLatin1String acc("me@example.com");

    // First run: plaintext, with an account in it, and a migration requested.
    {
        TackyBackend backend;
        QVERIFY(backend.start(args));
        QSignalSpy results(&backend, &TackyBackend::result);

        int tok = backend.request(QStringLiteral("storage"),
                                  QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, tok).isValid(), 5000);
        QCOMPARE(resultFor(results, tok).toString(), QString("plaintext"));

        backend.notify(QStringLiteral("account"), QStringLiteral("add"),
                       QVariantMap{{QStringLiteral("acc"), acc},
                                   {QStringLiteral("password"),
                                    QStringLiteral("hunter2")}});

        tok = backend.request(QStringLiteral("storage"),
                              QStringLiteral("requestEncrypt"));
        int status = backend.request(QStringLiteral("storage"),
                                     QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, status).isValid(), 5000);
        QCOMPARE(resultFor(results, status).toString(),
                 QString("pending-encrypt"));
        backend.stop();
    }

    // Second run: the marker is still there, so this is the pre-boot gate. The
    // migration runs here and drops into normal operation in the same process.
    {
        TackyBackend backend;
        QVERIFY(backend.start(args));
        QSignalSpy results(&backend, &TackyBackend::result);

        int tok = backend.request(QStringLiteral("storage"),
                                  QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, tok).isValid(), 5000);
        QCOMPARE(resultFor(results, tok).toString(),
                 QString("pending-encrypt"));

        // SQLCipher's KDF is deliberately expensive and runs once per db, so
        // this is the slow call in the suite.
        tok = backend.request(
            QStringLiteral("storage"), QStringLiteral("encrypt"),
            QVariantMap{{QStringLiteral("passphrase"), kPass}});
        int status = backend.request(QStringLiteral("storage"),
                                     QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, status).isValid(), 30000);
        QCOMPARE(resultFor(results, status).toString(), QString("unlocked"));

        // And the account survived it - the migration carried the rows, not
        // just the file.
        const int list = backend.request(QStringLiteral("account"),
                                         QStringLiteral("list"), QVariantMap{});
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, list).isValid(), 5000);
        QCOMPARE(resultFor(results, list).toStringList(), QStringList({acc}));
        backend.stop();
    }

    // Third run: encrypted now, so nothing but `storage` answers until the
    // passphrase does. A wrong one is refused in place, without a restart.
    {
        TackyBackend backend;
        QVERIFY(backend.start(args));
        QSignalSpy results(&backend, &TackyBackend::result);
        QSignalSpy errors(&backend, &TackyBackend::error);

        int tok = backend.request(QStringLiteral("storage"),
                                  QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, tok).isValid(), 5000);
        QCOMPARE(resultFor(results, tok).toString(), QString("locked"));

        const int wrong = backend.request(
            QStringLiteral("storage"), QStringLiteral("unlock"),
            QVariantMap{{QStringLiteral("passphrase"), QStringLiteral("nope")}});
        QTRY_VERIFY_WITH_TIMEOUT(!errorFor(errors, wrong).isEmpty(), 30000);
        QVERIFY(errorFor(errors, wrong).contains(QLatin1String("passphrase")));

        backend.request(
            QStringLiteral("storage"), QStringLiteral("unlock"),
            QVariantMap{{QStringLiteral("passphrase"), kPass}});
        const int list = backend.request(QStringLiteral("account"),
                                         QStringLiteral("list"), QVariantMap{});
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, list).isValid(), 30000);
        QCOMPARE(resultFor(results, list).toStringList(), QStringList({acc}));
        backend.stop();
    }
}

// Cancelling at the pre-boot gate leaves the store as it was - and has to leave
// the app usable, in this same process. tacky defers installing `account`,
// `setting`, `audio` and `log` while a migration is pending, so the cancel has
// to complete that deferred boot; without it `status` answered `plaintext`
// while every other module was still missing, and the app came up reporting
// "delegates method ... to undefined component" for each one it asked.
void TestStorage::cancellingAtTheGateStillOpensTheApp() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList args{QStringLiteral("-transient"), QStringLiteral("0"),
                           QStringLiteral("-config-dir"), dir.filePath("cfg"),
                           QStringLiteral("-data-dir"), dir.filePath("data"),
                           QStringLiteral("-cache-dir"), dir.filePath("cache")};
    const QLatin1String acc("me@example.com");

    {
        TackyBackend backend;
        QVERIFY(backend.start(args));
        QSignalSpy results(&backend, &TackyBackend::result);

        backend.notify(QStringLiteral("account"), QStringLiteral("add"),
                       QVariantMap{{QStringLiteral("acc"), acc},
                                   {QStringLiteral("password"),
                                    QStringLiteral("hunter2")}});
        backend.request(QStringLiteral("storage"),
                        QStringLiteral("requestEncrypt"));
        const int status = backend.request(QStringLiteral("storage"),
                                           QStringLiteral("status"));
        QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, status).isValid(), 5000);
        QCOMPARE(resultFor(results, status).toString(),
                 QString("pending-encrypt"));
        backend.stop();
    }

    // The gate, saying no.
    TackyBackend backend;
    QVERIFY(backend.start(args));
    QSignalSpy results(&backend, &TackyBackend::result);
    QSignalSpy errors(&backend, &TackyBackend::error);

    int tok = backend.request(QStringLiteral("storage"),
                              QStringLiteral("status"));
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, tok).isValid(), 5000);
    QCOMPARE(resultFor(results, tok).toString(), QString("pending-encrypt"));

    backend.request(QStringLiteral("storage"), QStringLiteral("cancelPending"));
    tok = backend.request(QStringLiteral("storage"), QStringLiteral("status"));
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, tok).isValid(), 5000);
    QCOMPARE(resultFor(results, tok).toString(), QString("plaintext"));

    // The store is untouched, and - the part that broke - every module the app
    // asks for on the way up now answers.
    const int list = backend.request(QStringLiteral("account"),
                                     QStringLiteral("list"), QVariantMap{});
    const int level = backend.request(
        QStringLiteral("setting"), QStringLiteral("get"),
        QVariantMap{{QStringLiteral("key"), QStringLiteral("log_level")}});
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, list).isValid(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, level).isValid(), 5000);
    QCOMPARE(resultFor(results, list).toStringList(), QStringList({acc}));

    for (const QList<QVariant> &e : errors)
        QVERIFY2(!e.at(1).toString().contains(QLatin1String("undefined component")),
                 qPrintable(e.at(1).toString()));
    backend.stop();
}

QTEST_MAIN(TestStorage)
#include "tst_storage.moc"
