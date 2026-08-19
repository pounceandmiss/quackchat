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
    void theLevelReachesTheBackend();
    void theWebrtcSwitchBecomesANativeLevel();
    void anExplicitDebugFileOwnsTheSink();
    void eachDebugFlagOwnsOnlyItsOwnSetting();
    void loggingToAFileRoundTripsThroughTheBackend();
    void theBackendAnswersWithThePathItChose();
    void nothingIsForwardedWhileTheSinkIsStderr();
};

// Everything else bound to the backend reseeds on the same connect, so the log
// frames have to be picked out of the crowd.
static QVariantList logCalls(const QSignalSpy &sent, const char *method) {
    QVariantList out;
    for (const QList<QVariant> &call : sent)
        if (call.at(0).toString() == "log" && call.at(1).toString() == method)
            out.append(call.at(2));
    return out;
}

// Feed the model the stored value the way the backend announces one.
static void store(AppSettings *settings, const QString &key,
                  const QString &value) {
    settings->handleEvent(QStringLiteral("setting"), QStringLiteral("Changed"),
                          QVariantMap{{QStringLiteral("key"), key},
                                      {QStringLiteral("value"), value}});
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
    QCOMPARE(logCalls(sent, "setenabled").size(), 1);
    QCOMPARE(logCalls(sent, "setenabled").first().toMap().value("enabled").toBool(),
             false);

    sent.clear();
    store(app.settings(), QStringLiteral("log_to_file"), QStringLiteral("1"));
    QCOMPARE(logCalls(sent, "setenabled").size(), 1);
    QCOMPARE(
        logCalls(sent, "setenabled").first().toMap().value("enabled").toBool(),
        true);
}

// The level is stored, but what it drives is per process, so it goes out on
// every connect as well as every change.
void TestLogging::theLevelReachesTheBackend() {
    AppController app;
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    QCOMPARE(logCalls(sent, "setlevel").size(), 1);
    // tacky's own default, until something stored says otherwise.
    QCOMPARE(logCalls(sent, "setlevel").first().toMap().value("level").toString(),
             QString("warning"));

    sent.clear();
    store(app.settings(), QStringLiteral("log_level"), QStringLiteral("debug"));
    QCOMPARE(logCalls(sent, "setlevel").size(), 1);
    QCOMPARE(logCalls(sent, "setlevel").first().toMap().value("level").toString(),
             QString("debug"));
}

// A switch here, a level there: the libraries filter their own output and jlog
// does not filter it again, so "on" has to name a level for them to stop at.
void TestLogging::theWebrtcSwitchBecomesANativeLevel() {
    AppController app;
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    QCOMPARE(
        logCalls(sent, "setnativelevel").first().toMap().value("level").toString(),
        QString("none"));

    sent.clear();
    store(app.settings(), QStringLiteral("log_native"), QStringLiteral("1"));
    QCOMPARE(logCalls(sent, "setnativelevel").size(), 1);
    QCOMPARE(
        logCalls(sent, "setnativelevel").first().toMap().value("level").toString(),
        QString("debug"));
    // No source: both libraries move together, which is what one switch means.
    QVERIFY(!logCalls(sent, "setnativelevel").first().toMap().contains("source"));
}

// Each flag owns its own setting, and only its own: --debug-level says nothing
// about the file or the native loggers.
void TestLogging::eachDebugFlagOwnsOnlyItsOwnSetting() {
    AppController app;
    app.setDebugArgs({QStringLiteral("debug"), {}, {}, {}});
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    QCOMPARE(logCalls(sent, "setlevel").size(), 0);
    QCOMPARE(logCalls(sent, "setenabled").size(), 1);
    QCOMPARE(logCalls(sent, "setnativelevel").size(), 1);

    AppController native;
    native.setDebugArgs({{}, {}, QStringLiteral("info"), {}});
    QSignalSpy nativeSent(native.backend(), &TackyBackend::sent);
    emit native.backend()->connected();
    QCOMPARE(logCalls(nativeSent, "setnativelevel").size(), 0);
    QCOMPARE(logCalls(nativeSent, "setlevel").size(), 1);
}

// --debug-file names the sink for the whole run, so the toggle stays out of it
// - otherwise the first connect would move the log the user asked for.
void TestLogging::anExplicitDebugFileOwnsTheSink() {
    AppController app;
    app.setDebugArgs({{}, QStringLiteral("/tmp/quack-test.log"), {}, {}});
    QSignalSpy sent(app.backend(), &TackyBackend::sent);

    emit app.backend()->connected();
    store(app.settings(), QStringLiteral("log_to_file"), QStringLiteral("1"));
    QCOMPARE(logCalls(sent, "setenabled").size(), 0);
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

    // Below the default threshold, so it is dropped until the level moves.
    backend.notify(QStringLiteral("log"), QStringLiteral("write"),
                   logWriteArgs(QtInfoMsg, ctx, QStringLiteral("only at info")));
    QTest::qWait(200);
    QVERIFY(log.seek(0));
    QVERIFY(!QString::fromUtf8(log.readAll())
                 .contains(QLatin1String("only at info")));

    backend.notify(QStringLiteral("log"), QStringLiteral("setlevel"),
                   QVariantMap{{QStringLiteral("level"),
                                QStringLiteral("info")}});
    backend.notify(QStringLiteral("log"), QStringLiteral("write"),
                   logWriteArgs(QtInfoMsg, ctx, QStringLiteral("now at info")));
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            return log.seek(0) && QString::fromUtf8(log.readAll())
                                      .contains(QLatin1String("now at info"));
        }(),
        5000);

    // The native loggers take the same levels and answer for themselves.
    backend.notify(QStringLiteral("log"), QStringLiteral("setnativelevel"),
                   QVariantMap{{QStringLiteral("level"),
                                QStringLiteral("debug")}});
    QString native;
    const int nativeTok = backend.request(
        QStringLiteral("log"), QStringLiteral("getnativelevel"),
        QVariantMap{{QStringLiteral("source"),
                     QStringLiteral("libdatachannel")}});
    QTRY_VERIFY_WITH_TIMEOUT(
        [&] {
            for (const QList<QVariant> &r : results)
                if (r.at(0).toInt() == nativeTok)
                    native = r.at(1).toString();
            return !native.isEmpty();
        }(),
        5000);
    QCOMPARE(native, QString("debug"));

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
    store(app.settings(), QStringLiteral("log_to_file"), QStringLiteral("1"));

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
