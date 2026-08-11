// The call state machine, driven with the JSON the backend really emits, plus
// one round trip through a real libtacky to check the wire shape of
// `calls start` against the actual Tcl rather than against the reference docs.
#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "CallsModel.h"
#include "TackyBackend.h"

static void feed(CallsModel &m, const QByteArray &json) {
    const QJsonArray a = QJsonDocument::fromJson(json).array();
    m.handleEvent(a.at(1).toString(), a.at(2).toString(), a.at(3).toVariant());
}

static QString state(const CallsModel &m, int row = 0) {
    return m.data(m.index(row), CallsModel::StateRole).toString();
}

class TestCalls : public QObject {
    Q_OBJECT
private slots:
    void outgoingRingsThenConnects();
    void incomingIsAcceptedLocally();
    void ringingOnlyLiftsFromCalling();
    void warningIsNotTerminalAndActiveClearsIt();
    void recoveredMediaClearsTheWarning();
    void failedKeepsReason();
    void terminalRowsSurviveUntilDismissed();
    void ignoresEventsForUnknownSid();
    void ignoresDuplicateSid();
    void tracksCallsOnSeveralAccounts();
    void bothEndsOfACallBetweenOurOwnAccountsAreSeparateRows();
    void hangupEndsRowWithoutAnEvent();
    void sendsTheAccountAndSidTheRowHolds();
    void perCallDeviceOverrideNamesBothEndpoints();
    // Against a real backend:
    void startEmitsOutgoingWithRealSid();
    void startOnABadJidReportsFailure();
    void startOnAMissingAccountReportsFailure();
};

// Caller: propose -> a peer device alerts -> media comes up -> teardown.
void TestCalls::outgoingRingsThenConnects() {
    CallsModel m;
    QSignalSpy added(&m, &CallsModel::callAdded);

    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-1","to":"friend@host"}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(added.count(), 1);
    QCOMPARE(added.first().at(0).toString(), QString("me@host"));
    QCOMPARE(added.first().at(1).toString(), QString("tk-1"));
    QCOMPARE(m.data(m.index(0), CallsModel::AccountRole).toString(),
             QString("me@host"));
    QCOMPARE(m.data(m.index(0), CallsModel::PeerRole).toString(),
             QString("friend@host"));
    QCOMPARE(m.data(m.index(0), CallsModel::DirectionRole).toString(),
             QString("outgoing"));
    QCOMPARE(state(m), QString("calling"));
    QVERIFY(!m.data(m.index(0), CallsModel::TerminalRole).toBool());

    feed(m, R"(["event","calls","Ringing",{"acc":"me@host","sid":"tk-1"}])");
    QCOMPARE(state(m), QString("ringing"));

    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-1"}])");
    QCOMPARE(state(m), QString("active"));

    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-1"}])");
    QCOMPARE(state(m), QString("ended"));
    QVERIFY(m.data(m.index(0), CallsModel::TerminalRole).toBool());
}

// Callee: nothing is emitted between our proceed and <Active>, so accept()
// has to move the row itself or the window sits on "Incoming call" throughout
// ICE.
void TestCalls::incomingIsAcceptedLocally() {
    CallsModel m;
    feed(m, R"(["event","calls","Incoming",
        {"acc":"me@host","sid":"tk-2","from":"friend@host"}])");
    QCOMPARE(m.data(m.index(0), CallsModel::DirectionRole).toString(),
             QString("incoming"));
    QCOMPARE(state(m), QString("incoming"));

    m.accept(QStringLiteral("me@host"), QStringLiteral("tk-2"));
    QCOMPARE(state(m), QString("connecting"));

    // Accepting twice must not walk the state backwards.
    m.accept(QStringLiteral("me@host"), QStringLiteral("tk-2"));
    QCOMPARE(state(m), QString("connecting"));

    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-2"}])");
    QCOMPARE(state(m), QString("active"));
}

// <Ringing> repeats once per answering device and can land late. It only ever
// lifts a call that is still merely proposed.
void TestCalls::ringingOnlyLiftsFromCalling() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-3","to":"friend@host"}])");
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-3"}])");
    feed(m, R"(["event","calls","Ringing",{"acc":"me@host","sid":"tk-3"}])");
    QCOMPARE(state(m), QString("active"));
}

void TestCalls::warningIsNotTerminalAndActiveClearsIt() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-4","to":"friend@host"}])");
    feed(m, R"(["event","calls","Warning",
        {"acc":"me@host","sid":"tk-4","reason":"input device unavailable"}])");
    QCOMPARE(m.data(m.index(0), CallsModel::WarningRole).toString(),
             QString("input device unavailable"));
    QCOMPARE(state(m), QString("calling")); // informational only
    QVERIFY(!m.data(m.index(0), CallsModel::TerminalRole).toBool());

    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-4"}])");
    QCOMPARE(m.data(m.index(0), CallsModel::WarningRole).toString(), QString());
}

// ICE losing consent mid-call warns and leaves the call running; libdatachannel
// either recovers, which re-emits <Active>, or gives up into <Failed>. The
// recovery has to take the stale warning down with it.
void TestCalls::recoveredMediaClearsTheWarning() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-12","to":"friend@host"}])");
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-12"}])");
    feed(m, R"(["event","calls","Warning",
        {"acc":"me@host","sid":"tk-12","reason":"media path interrupted"}])");
    QCOMPARE(m.data(m.index(0), CallsModel::WarningRole).toString(),
             QString("media path interrupted"));
    QCOMPARE(state(m), QString("active")); // still up

    // The call never left "active", so this must not be skipped as a no-op.
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-12"}])");
    QCOMPARE(m.data(m.index(0), CallsModel::WarningRole).toString(), QString());
    QCOMPARE(state(m), QString("active"));
}

void TestCalls::failedKeepsReason() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-5","to":"friend@host"}])");
    // Custom delimiter: the reason text itself contains `)"`.
    feed(m, R"json(["event","calls","Failed",
        {"acc":"me@host","sid":"tk-5","reason":"media path failed (ICE/DTLS)"}])json");
    QCOMPARE(state(m), QString("failed"));
    QCOMPARE(m.data(m.index(0), CallsModel::ReasonRole).toString(),
             QString("media path failed (ICE/DTLS)"));
    QVERIFY(m.data(m.index(0), CallsModel::TerminalRole).toBool());
}

// The window shows the outcome, so nothing else may prune the row.
void TestCalls::terminalRowsSurviveUntilDismissed() {
    CallsModel m;
    QSignalSpy removed(&m, &CallsModel::callRemoved);
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-6","to":"friend@host"}])");
    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-6"}])");
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(removed.count(), 0);

    m.dismiss(QStringLiteral("me@host"), QStringLiteral("tk-6"));
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(0).toString(), QString("me@host"));
    QCOMPARE(removed.first().at(1).toString(), QString("tk-6"));
}

// A sid we never saw start must not conjure a row - a stray <Active> for
// someone else's session would otherwise open a window on nothing.
void TestCalls::ignoresEventsForUnknownSid() {
    CallsModel m;
    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-ghost"}])");
    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-ghost"}])");
    feed(m, R"(["event","calls","Warning",
        {"acc":"me@host","sid":"tk-ghost","reason":"x"}])");
    QCOMPARE(m.rowCount(), 0);
}

void TestCalls::ignoresDuplicateSid() {
    CallsModel m;
    feed(m, R"(["event","calls","Incoming",
        {"acc":"me@host","sid":"tk-7","from":"friend@host"}])");
    feed(m, R"(["event","calls","Incoming",
        {"acc":"me@host","sid":"tk-7","from":"friend@host"}])");
    QCOMPARE(m.rowCount(), 1);
}

void TestCalls::tracksCallsOnSeveralAccounts() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-8","to":"friend@host"}])");
    feed(m, R"(["event","calls","Incoming",
        {"acc":"other@host","sid":"tk-9","from":"someone@host"}])");
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(1), CallsModel::AccountRole).toString(),
             QString("other@host"));

    // Each ends on its own sid, leaving the other alone.
    feed(m, R"(["event","calls","Ended",{"acc":"me@host","sid":"tk-8"}])");
    QCOMPARE(state(m, 0), QString("ended"));
    QCOMPARE(state(m, 1), QString("incoming"));
}

// A sid names a session, not a call row: the caller picks it and the callee
// keeps it, so when both ends are accounts in this same app the two rows
// legitimately share one. Keying on the sid alone loses the second row
// entirely, and every later event for either end lands on whichever row won.
void TestCalls::bothEndsOfACallBetweenOurOwnAccountsAreSeparateRows() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-x","to":"other@host"}])");
    feed(m, R"(["event","calls","Incoming",
        {"acc":"other@host","sid":"tk-x","from":"me@host"}])");
    QCOMPARE(m.rowCount(), 2);

    // The callee end going away says nothing about the caller end. This is the
    // live case: another of that account's devices answers, so its client here
    // stops ringing and ends - while the call itself is up and audible.
    feed(m, R"(["event","calls","Ended",{"acc":"other@host","sid":"tk-x"}])");
    QCOMPARE(state(m, 0), QString("calling"));
    QCOMPARE(state(m, 1), QString("ended"));

    feed(m, R"(["event","calls","Active",{"acc":"me@host","sid":"tk-x"}])");
    QCOMPARE(state(m, 0), QString("active"));
    QCOMPARE(state(m, 1), QString("ended"));
}

// hangup/reject stay honest even when the backend has already forgotten the
// sid (the peer terminated first), which is the one case where no <Ended> is
// coming back for us.
void TestCalls::hangupEndsRowWithoutAnEvent() {
    CallsModel m;
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-10","to":"friend@host"}])");
    m.hangup(QStringLiteral("me@host"), QStringLiteral("tk-10"));
    QCOMPARE(state(m), QString("ended"));

    feed(m, R"(["event","calls","Incoming",
        {"acc":"me@host","sid":"tk-11","from":"friend@host"}])");
    m.reject(QStringLiteral("me@host"), QStringLiteral("tk-11"));
    QCOMPARE(state(m, 1), QString("ended"));
}

// Callers pass a sid and nothing else; the account it belongs to comes off the
// row. With two accounts in play, sending on the wrong one would reach a client
// that has never heard of the sid and silently do nothing.
void TestCalls::sendsTheAccountAndSidTheRowHolds() {
    TackyBackend backend; // never started: `sent` still reports what was asked
    CallsModel m;
    m.setBackend(&backend);
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-a","to":"friend@host"}])");
    feed(m, R"(["event","calls","Incoming",
        {"acc":"other@host","sid":"tk-b","from":"someone@host"}])");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.hangup(QStringLiteral("me@host"), QStringLiteral("tk-a"),
             QStringLiteral("busy"));
    m.accept(QStringLiteral("other@host"), QStringLiteral("tk-b"));

    QCOMPARE(sent.count(), 2);
    const QList<QVariant> hangup = sent.at(0);
    QCOMPARE(hangup.at(0).toString(), QString("calls"));
    QCOMPARE(hangup.at(1).toString(), QString("hangup"));
    QCOMPARE(hangup.at(2).toMap().value("acc").toString(), QString("me@host"));
    QCOMPARE(hangup.at(2).toMap().value("sid").toString(), QString("tk-a"));
    QCOMPARE(hangup.at(2).toMap().value("reason").toString(), QString("busy"));

    const QList<QVariant> accept = sent.at(1);
    QCOMPARE(accept.at(1).toString(), QString("accept"));
    QCOMPARE(accept.at(2).toMap().value("acc").toString(), QString("other@host"));
    QCOMPARE(accept.at(2).toMap().value("sid").toString(), QString("tk-b"));
    // No reason was given, so none is sent - the backend's own default stands.
    QVERIFY(!accept.at(2).toMap().contains("reason"));
}

// Both endpoints go in the one request, and "" is a real value there: it means
// the system default, not "leave this one alone".
void TestCalls::perCallDeviceOverrideNamesBothEndpoints() {
    TackyBackend backend;
    CallsModel m;
    m.setBackend(&backend);
    feed(m, R"(["event","calls","Outgoing",
        {"acc":"me@host","sid":"tk-c","to":"friend@host"}])");

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.setDevices(QStringLiteral("me@host"), QStringLiteral("tk-c"),
                 QStringLiteral("mic-2"), QString());

    QCOMPARE(sent.count(), 1);
    const QVariantMap args = sent.first().at(2).toMap();
    QCOMPARE(sent.first().at(1).toString(), QString("setDevices"));
    QCOMPARE(args.value("sid").toString(), QString("tk-c"));
    QCOMPARE(args.value("input").toString(), QString("mic-2"));
    QVERIFY(args.contains("output"));
    QCOMPARE(args.value("output").toString(), QString());

    // An unknown sid has no account to address, so nothing goes out at all.
    m.setDevices(QStringLiteral("me@host"), QStringLiteral("tk-nope"),
                 QStringLiteral("mic-2"), QString());
    QCOMPARE(sent.count(), 1);
}

// `calls start` emits <Outgoing> before it writes the propose, so this works
// against an account that exists but was never connected - and it pins the
// argument names (acc/to), which the reference's signature leaves out.
void TestCalls::startEmitsOutgoingWithRealSid() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    CallsModel m;
    m.setBackend(&backend);
    m.start(QStringLiteral("me@example.com"), QStringLiteral("friend@example.com"));

    QTRY_VERIFY_WITH_TIMEOUT(m.rowCount() == 1, 5000);
    QCOMPARE(m.data(m.index(0), CallsModel::DirectionRole).toString(),
             QString("outgoing"));
    QCOMPARE(m.data(m.index(0), CallsModel::PeerRole).toString(),
             QString("friend@example.com"));
    QCOMPARE(m.data(m.index(0), CallsModel::AccountRole).toString(),
             QString("me@example.com"));
    QCOMPARE(state(m), QString("calling"));
    // The sid is the backend's own (`tk-` + 16 random bytes), not anything
    // this side made up.
    const QString sid = m.data(m.index(0), CallsModel::SidRole).toString();
    QVERIFY2(sid.startsWith("tk-"), qPrintable(sid));

    backend.stop();
}

// A refusal from inside `calls start` does come back on the token: the taco
// method wrapper catches it and routes it to the request's -onerror. A
// malformed peer JID is the realistic way in - `jid bare` rejects it.
void TestCalls::startOnABadJidReportsFailure() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    CallsModel m;
    m.setBackend(&backend);
    QSignalSpy failed(&m, &CallsModel::startFailed);
    m.start(QStringLiteral("me@example.com"), QStringLiteral("not a jid"));

    QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 5000);
    QCOMPARE(failed.first().at(0).toString(), QString("me@example.com"));
    QCOMPARE(failed.first().at(1).toString(), QString("not a jid"));
    QVERIFY(!failed.first().at(2).toString().isEmpty());
    QCOMPARE(m.rowCount(), 0);

    backend.stop();
}

// The other way in fails earlier, in taco's routing rather than in the method,
// and so never reaches the method wrapper. That used to mean no reply at all;
// taco_call now catches it too, so both failure modes reach startFailed and the
// token is always answered.
void TestCalls::startOnAMissingAccountReportsFailure() {
    TackyBackend backend;
    QVERIFY(backend.start());

    CallsModel m;
    m.setBackend(&backend);
    QSignalSpy failed(&m, &CallsModel::startFailed);
    m.start(QStringLiteral("nobody@example.com"), QStringLiteral("friend@example.com"));

    QTRY_VERIFY_WITH_TIMEOUT(failed.count() == 1, 5000);
    QCOMPARE(failed.first().at(0).toString(), QString("nobody@example.com"));
    QCOMPARE(failed.first().at(1).toString(), QString("friend@example.com"));
    QVERIFY(!failed.first().at(2).toString().isEmpty());
    QCOMPARE(m.rowCount(), 0);

    backend.stop();
}

QTEST_MAIN(TestCalls)
#include "tst_calls.moc"
