// Links libtacky.a and drives a real request/response round-trip through
// TackyBackend. Taco runs in-memory (transient), so `account list` returns
// an empty list.
#include <QtTest>
#include <QSignalSpy>
#include <QVariantList>

#include "BackendBinding.h"
#include "TackyBackend.h"

class TestBackend : public QObject {
    Q_OBJECT
private slots:
    void startsAndStops();
    void accountListRoundTrip();
    void errorOnUnknownModule();
    void sentReportsCallsWithoutABackend();
    void aRequestThatNeverWentOutIsAnswered();
    void stoppingFailsWhateverWasStillOut();
    void bindingCarriesEverySignalAModelNeeds();
    void optingOutLeavesOnlyTheReSeedBehind();
};

// A stand-in for any of the models: the three handlers bindBackend requires,
// and the re-read it drives on the connected edge.
class BoundModel : public QObject {
    Q_OBJECT
public:
    int events = 0, results = 0, errors = 0, reseeds = 0;
    void handleEvent(const QString &, const QString &, const QVariant &) { ++events; }
    void handleResult(int, const QVariant &) { ++results; }
    void handleError(int, const QString &) { ++errors; }
    void reseed() { ++reseeds; }
};

// Pinned here rather than once per model: the binding is the same for all of
// them, and a dropped connection is invisible until something goes unanswered.
void TestBackend::bindingCarriesEverySignalAModelNeeds() {
    TackyBackend backend;
    BoundModel m;
    bindBackend(&m, &backend, &BoundModel::reseed);

    emit backend.event("omemo", "Enabled", QVariantMap{});
    emit backend.result(1, QVariant());
    emit backend.error(1, QStringLiteral("nope"));
    emit backend.connected();

    QCOMPARE(m.events, 1);
    QCOMPARE(m.results, 1);
    QCOMPARE(m.errors, 1);
    QCOMPARE(m.reseeds, 1);
}

// The opt-out drops the re-read and nothing else: a failure is still heard.
void TestBackend::optingOutLeavesOnlyTheReSeedBehind() {
    TackyBackend backend;
    BoundModel m;
    bindBackendWithoutReseed(&m, &backend);

    emit backend.error(1, QStringLiteral("nope"));
    emit backend.connected();

    QCOMPARE(m.errors, 1);
    QCOMPARE(m.reseeds, 0);
}

void TestBackend::startsAndStops() {
    TackyBackend backend;
    QVERIFY(!backend.isRunning());
    QVERIFY(backend.start());
    QVERIFY(backend.isRunning());
    backend.stop();
    QVERIFY(!backend.isRunning());
}

void TestBackend::accountListRoundTrip() {
    TackyBackend backend;
    QVERIFY(backend.start());

    QSignalSpy spy(&backend, &TackyBackend::result);
    const int token = backend.request("account", "list", QVariantMap());

    QVERIFY2(spy.wait(5000), "no result signal within 5s");
    QCOMPARE(spy.count(), 1);

    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), token);
    // A fresh in-memory taco has no accounts -> empty list.
    QVERIFY(args.at(1).canConvert<QVariantList>());
    QCOMPARE(args.at(1).toList().size(), 0);

    backend.stop();
}

void TestBackend::errorOnUnknownModule() {
    TackyBackend backend;
    QVERIFY(backend.start());

    QSignalSpy okSpy(&backend, &TackyBackend::result);
    QSignalSpy errSpy(&backend, &TackyBackend::error);
    const int token = backend.request("no_such_module", "nope", QVariantMap());

    // Either an error reply arrives, or nothing does; a *result* must not.
    errSpy.wait(3000);
    if (errSpy.count() > 0)
        QCOMPARE(errSpy.takeFirst().at(0).toInt(), token);
    QCOMPARE(okSpy.count(), 0);

    backend.stop();
}

// The models are unit-tested with no interpreter running, where sendArray drops
// the call on the floor - so `sent` is the only evidence of what they asked for.
void TestBackend::sentReportsCallsWithoutABackend() {
    TackyBackend backend;
    QVERIFY(!backend.isRunning());

    QSignalSpy spy(&backend, &TackyBackend::sent);
    backend.notify("file", "download", QVariantMap{{"url", "http://h/a.png"}});
    backend.request("account", "list");

    QCOMPARE(spy.count(), 2);
    const QList<QVariant> first = spy.takeFirst();
    QCOMPARE(first.at(0).toString(), QString("file"));
    QCOMPARE(first.at(1).toString(), QString("download"));
    QCOMPARE(first.at(2).toMap().value("url").toString(), QString("http://h/a.png"));
    // A defaulted args stays a map rather than reaching the spy as invalid.
    QCOMPARE(spy.takeFirst().at(2).typeId(), QMetaType::QVariantMap);
}

// request() hands back a token whether or not the frame went anywhere, and the
// caller waits on it. A dropped frame therefore owes an answer.
void TestBackend::aRequestThatNeverWentOutIsAnswered() {
    TackyBackend backend; // never started
    QSignalSpy errors(&backend, &TackyBackend::error);

    const int tok = backend.request("account", "list", QVariantMap{});

    // Not during the call: the caller records this token after request()
    // returns, so an answer arriving inside it would find nothing to match.
    QCOMPARE(errors.count(), 0);

    QTRY_COMPARE(errors.count(), 1);
    QCOMPARE(errors.first().at(0).toInt(), tok);
    QVERIFY(!errors.first().at(1).toString().isEmpty());
}

void TestBackend::stoppingFailsWhateverWasStillOut() {
    TackyBackend backend;
    QVERIFY(backend.start());
    // No account, so this cannot be answered before the stop below.
    const int tok = backend.request("message", "history",
                                    QVariantMap{{"acc", "nobody@h"},
                                                {"chat", "x@h"},
                                                {"limit", 50}});
    QSignalSpy errors(&backend, &TackyBackend::error);
    backend.stop();

    QTRY_VERIFY(!errors.isEmpty());
    bool sawOurs = false;
    for (const QList<QVariant> &row : errors)
        sawOurs = sawOurs || row.at(0).toInt() == tok;
    QVERIFY2(sawOurs, "a token still owed when the transport went away must be "
                      "failed, not dropped");
}

QTEST_MAIN(TestBackend)
#include "tst_backend.moc"
