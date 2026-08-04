// Loads the app QML headless with the real registered types. Binding errors
// (ReferenceError etc.) only surface as warnings, so collect and fail on them.
#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>

#include "AppController.h"

class TestQmlLoad : public QObject {
    Q_OBJECT

    static void assertNoQmlErrors(const QStringList &warnings) {
        for (const QString &w : warnings)
            if (w.contains("ReferenceError") || w.contains("is not defined") ||
                w.contains("TypeError"))
                QFAIL(qPrintable("QML error: " + w));
    }

private slots:
    void loadsApp() {
        QStringList warnings;
        QQmlApplicationEngine e;
        // create the App singleton up front; backend stays unstarted
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());
        auto *win = qobject_cast<QQuickWindow *>(e.rootObjects().first());
        QVERIFY(win);
        win->grabWindow(); // force a render so lazy bindings evaluate
        QCoreApplication::processEvents();
        assertNoQmlErrors(warnings);
    }

    void loadsMultiWindow() {
        QStringList warnings;
        QQmlEngine e;
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        QQmlComponent chatWin(&e, "Quack", "ChatWindow");
        QVERIFY2(chatWin.isReady(), qPrintable(chatWin.errorString()));
        QScopedPointer<QObject> cw(chatWin.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"chatJid", "friend@example.com"},
             {"chatName", "Friend"}}));
        QVERIFY(!cw.isNull());

        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant ret;
        QVERIFY(QMetaObject::invokeMethod(mgr, "newShell",
                                          Q_RETURN_ARG(QVariant, ret),
                                          Q_ARG(QVariant, QVariant(QString()))));
        QVERIFY2(ret.value<QObject *>() != nullptr, "newShell returned no window");

        assertNoQmlErrors(warnings);
    }
};

QTEST_MAIN(TestQmlLoad)
#include "tst_qmlload.moc"
