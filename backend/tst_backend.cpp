// Links libtacky.a and drives a real request/response round-trip through
// TackyBackend. Taco runs in-memory (transient), so `account list` returns
// an empty list.
#include <QtTest>
#include <QSignalSpy>
#include <QVariantList>

#include "TackyBackend.h"

class TestBackend : public QObject {
    Q_OBJECT
private slots:
    void startsAndStops();
    void accountListRoundTrip();
    void errorOnUnknownModule();
    void sentReportsCallsWithoutABackend();
};

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

QTEST_MAIN(TestBackend)
#include "tst_backend.moc"
