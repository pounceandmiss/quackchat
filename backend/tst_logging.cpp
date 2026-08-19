// What a bug report is made of: Qt's messages going into the backend's log, and
// the toggle that gives that log a file to land in. The two round trips against
// a real backend are the only thing proving the argument names match the Tcl.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AppController.h"
#include "LogBridge.h"
#include "TackyBackend.h"

class TestLogging : public QObject {
    Q_OBJECT
private slots:
    void qtLevelsBecomeTackyLevels();
    void categoriesHangUnderOneRoot();
    void ourOwnWireLoggingIsNotForwarded();
    void theToggleReachesTheBackend();
    void anExplicitDebugFileOwnsTheSink();
    void loggingToAFileRoundTripsThroughTheBackend();
    void theBackendAnswersWithThePathItChose();
    void nothingIsForwardedWhileTheSinkIsStderr();
};

// Everything else bound to the backend reseeds on the same connect, so the log
// frames have to be picked out of the crowd.
static QVariantList setEnabledCalls(const QSignalSpy &sent) {
    QVariantList out;
    for (const QList<QVariant> &call : sent)
        if (call.at(0).toString() == "log" &&
            call.at(1).toString() == "setenabled")
            out.append(call.at(2));
    return out;
}

static QVariantMap args(QtMsgType type, const char *category,
                        const QString &msg = QStringLiteral("hi")) {
    QMessageLogContext ctx(nullptr, 0, nullptr, category);
    return logWriteArgs(type, ctx, msg);
}

void TestLogging::qtLevelsBecomeTackyLevels() {
    QCOMPARE(args(QtDebugMsg, "quack.calls").value("level").toString(),
             QString("debug"));
    QCOMPARE(args(QtInfoMsg, "quack.calls").value("level").toString(),
             QString("info"));
    QCOMPARE(args(QtWarningMsg, "quack.calls").value("level").toString(),
             QString("warning"));
    // tacky has no `critical`, and a Qt critical is what its `error` means.
    QCOMPARE(args(QtCriticalMsg, "quack.calls").value("level").toString(),
             QString("error"));
    QCOMPARE(args(QtFatalMsg, "quack.calls").value("level").toString(),
             QString("fatal"));
    QCOMPARE(args(QtWarningMsg, "quack.calls", QStringLiteral("boom"))
                 .value("text")
                 .toString(),
             QString("boom"));
}

// One root over the lot, so `log setlevel {obj: "frontend"}` moves everything
// we log without touching what the backend logs about itself.
void TestLogging::categoriesHangUnderOneRoot() {
    QCOMPARE(args(QtWarningMsg, "quack.calls").value("obj").toString(),
             QString("frontend.quack.calls"));
    // Qt's own name for a message logged without a category.
    QCOMPARE(args(QtWarningMsg, "default").value("obj").toString(),
             QString("frontend"));
    QCOMPARE(args(QtWarningMsg, nullptr).value("obj").toString(),
             QString("frontend"));
}

// The wire category logs every frame that goes out, so forwarding it would send
// a frame to report having sent a frame.
void TestLogging::ourOwnWireLoggingIsNotForwarded() {
    QVERIFY(args(QtDebugMsg, "quack.wire").isEmpty());
    QVERIFY(args(QtWarningMsg, "quack.wire").isEmpty());
}

// The stored setting is not: it is the backend's per-process sink, so it has to
// be re-sent whenever the link comes up as well as when the user flips it.
void TestLogging::theToggleReachesTheBackend() {
    AppController app;
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    QCOMPARE(setEnabledCalls(sent).size(), 1);
    QCOMPARE(setEnabledCalls(sent).first().toMap().value("enabled").toBool(),
             false);

    sent.clear();
    app.settings()->handleEvent(
        QStringLiteral("setting"), QStringLiteral("Changed"),
        QVariantMap{{QStringLiteral("key"), QStringLiteral("log_to_file")},
                    {QStringLiteral("value"), QStringLiteral("1")}});
    QCOMPARE(setEnabledCalls(sent).size(), 1);
    QCOMPARE(setEnabledCalls(sent).first().toMap().value("enabled").toBool(), true);
}

// --debug-file names the sink for the whole run, so the toggle stays out of it
// - otherwise the first connect would move the log the user asked for.
void TestLogging::anExplicitDebugFileOwnsTheSink() {
    AppController app;
    app.setDebugArgs(QString(), QStringLiteral("/tmp/quack-test.log"));
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    app.settings()->handleEvent(
        QStringLiteral("setting"), QStringLiteral("Changed"),
        QVariantMap{{QStringLiteral("key"), QStringLiteral("log_to_file")},
                    {QStringLiteral("value"), QStringLiteral("1")}});
    QCOMPARE(setEnabledCalls(sent).size(), 0);
}

// Against the real module: the backend picks the path, and what we write
// through it lands there.
void TestLogging::loggingToAFileRoundTripsThroughTheBackend() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TackyBackend backend;
    QVERIFY(backend.start({QStringLiteral("-transient"), QStringLiteral("0"),
                           QStringLiteral("-config-dir"), dir.filePath("cfg"),
                           QStringLiteral("-data-dir"), dir.filePath("data"),
                           QStringLiteral("-cache-dir"), dir.filePath("cache")}));

    backend.notify(QStringLiteral("log"), QStringLiteral("setenabled"),
                   QVariantMap{{QStringLiteral("enabled"), true}});
    QMessageLogContext ctx(nullptr, 0, nullptr, "quack.test");
    backend.notify(QStringLiteral("log"), QStringLiteral("write"),
                   logWriteArgs(QtWarningMsg, ctx,
                                QStringLiteral("a line from the frontend")));

    QString path;
    QSignalSpy results(&backend, &TackyBackend::result);
    const int tok = backend.request(QStringLiteral("log"),
                                    QStringLiteral("getfile"));
    QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(), 5000);
    for (const QList<QVariant> &r : results)
        if (r.at(0).toInt() == tok)
            path = r.at(1).toString();
    QCOMPARE(path, QDir(dir.filePath("cache")).filePath("tacky.log"));

    QFile log(path);
    QTRY_VERIFY_WITH_TIMEOUT(log.exists(), 5000);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(log.readAll());
    QVERIFY(text.contains(QLatin1String("a line from the frontend")));
    // Tagged with the category it was logged under, so a report says which part
    // of the GUI spoke.
    QVERIFY(text.contains(QLatin1String("frontend.quack.test")));

    backend.stop();
}

// The path is what the export offers, and it is the backend's to choose, so it
// is read back rather than guessed at. Empty until there is a file, which is
// what the button in the settings page watches.
void TestLogging::theBackendAnswersWithThePathItChose() {
    AppController app;
    QVERIFY(app.backend()->start({QStringLiteral("-transient"),
                                  QStringLiteral("1")}));
    QTRY_VERIFY_WITH_TIMEOUT(app.backend()->isRunning(), 5000);
    QVERIFY(app.logPath().isEmpty());

    QSignalSpy changed(&app, &AppController::logPathChanged);
    app.settings()->handleEvent(
        QStringLiteral("setting"), QStringLiteral("Changed"),
        QVariantMap{{QStringLiteral("key"), QStringLiteral("log_to_file")},
                    {QStringLiteral("value"), QStringLiteral("1")}});

    QTRY_VERIFY_WITH_TIMEOUT(!app.logPath().isEmpty(), 5000);
    QCOMPARE(changed.count(), 1);
    QVERIFY(app.logPath().endsWith(QLatin1String("tacky.log")));
    QCOMPARE(app.logFolder(),
             QUrl::fromLocalFile(QFileInfo(app.logPath()).absolutePath()));

    app.backend()->stop();
}

// Forwarding to a backend still writing to stderr would print every message
// twice: once from the handler we chained to, once from the backend.
void TestLogging::nothingIsForwardedWhileTheSinkIsStderr() {
    // Static: the handler keeps the pointer for the life of the process, and
    // there is no uninstalling it - chaining a second time would make the
    // handler its own predecessor.
    static TackyBackend backend;
    installLogBridge(&backend);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    setLogBridgeActive(false);
    qWarning("into the void");
    QCOMPARE(sent.count(), 0);

    setLogBridgeActive(true);
    qWarning("and into the file");
    // Queued, so it is on the way rather than already sent.
    QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 5000);
    QCOMPARE(sent.first().at(1).toString(), QString("write"));
    setLogBridgeActive(false);
}

QTEST_MAIN(TestLogging)
#include "tst_logging.moc"
