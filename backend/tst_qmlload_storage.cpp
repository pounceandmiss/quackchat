// The local storage encryption UI: the startup gate that stands in front of a
// locked or mid-migration store, and the Preferences card that asks for one.
#include <QtTest>
#include <QQmlComponent>
#include <QSignalSpy>

#include "AppController.h"
#include "StorageController.h"
#include "TackyBackend.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestStorageUi : public QObject {
    Q_OBJECT

    // Puts App.storage into one of tacky's five states. The backend is never
    // started, so `refresh` gets a token and no answer; this supplies the
    // answer the way the transport would.
    //
    // Tokens are handed out in order, so a throwaway request first names the
    // one the refresh is about to get. Counting the frames a spy saw would not:
    // loading the UI sends plenty of its own, and a notify consumes no token.
    static void setStatus(AppController *app, const QString &status) {
        const int probe = app->backend()->request(QStringLiteral("storage"),
                                                  QStringLiteral("status"));
        app->storage()->refresh();
        app->storage()->handleResult(probe + 1, status);
    }

    static QQuickWindow *loadMain(AppEngine &e) {
        e.loadFromModule("Quack", "Main");
        if (e.rootObjects().isEmpty())
            return nullptr;
        auto *win = qobject_cast<QQuickWindow *>(e.rootObjects().first());
        if (!win)
            return nullptr;
        win->show();
        if (!QTest::qWaitForWindowExposed(win))
            return nullptr;
        QCoreApplication::processEvents();
        return win;
    }

    // The gate is a Popup, not an item: findItem walks childItems() and a
    // Popup is not in that tree at all. Its *contents* are, once it is open -
    // they hang off the window's overlay - so the parts below are still found
    // the usual way.
    static QObject *gate(QQuickWindow *win) {
        return win->findChild<QObject *>(QStringLiteral("storageGate"));
    }

    static QQuickWindow *openPreferences(QQmlEngine &e,
                                         QScopedPointer<QObject> &holder) {
        QQmlComponent comp(&e, "Quack", "AppSettingsWindow");
        if (!comp.isReady()) {
            qWarning("%s", qPrintable(comp.errorString()));
            return nullptr;
        }
        holder.reset(comp.create());
        auto *win = qobject_cast<QQuickWindow *>(holder.data());
        if (!win)
            return nullptr;
        // Taller than the shipped window so every card is in the viewport at
        // once: what is scrolled out is still findable, but not clickable.
        win->setHeight(1400);
        if (!QTest::qWaitForWindowExposed(win))
            return nullptr;
        win->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();
        return win;
    }

private slots:
    void nothingIsGatedWhileTheStoreIsOpen();
    void aLockedStoreGatesTheShellWithNoWayPast();
    void theGateIsRealEstateNotJustObjectNames();
    void aPendingEncryptWarnsAndTakesAPassphrase();
    void aPendingDecryptAsksForNoPassphrase();
    void aRefusedPassphraseIsShownAndRetriedInPlace();
    void theCardNamesTheStateAndTheOneActionForIt();
    void encryptingIsConfirmedBeforeItIsRequested();
    void theConfirmationOpensOverPreferencesNotTheShell();
    void aPendingRequestIsCancelledStraightFromTheCard();
};

// The common case: nothing to ask, so nothing in the way. An unanswered status
// is also not a gate - but it is not "ready" either, which is what holds the
// account list back rather than a dialog.
void TestStorageUi::nothingIsGatedWhileTheStoreIsOpen() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    // Never answered.
    QVERIFY(!app->storage()->ready());
    QObject *g = gate(win);
    QVERIFY2(!g || !g->property("visible").toBool(),
             "the gate showed before the status was known");

    for (const char *open : {"plaintext", "unlocked"}) {
        setStatus(app, QString::fromLatin1(open));
        QCoreApplication::processEvents();
        QVERIFY(app->storage()->ready());
        g = gate(win);
        QVERIFY2(!g || !g->property("visible").toBool(),
                 qPrintable(QStringLiteral("the gate showed on %1")
                                .arg(QLatin1String(open))));
    }
    e.assertNoErrors();
}

// There is nothing else the app can do with the store locked, so unlike the
// migration gate this one offers no way out at all.
void TestStorageUi::aLockedStoreGatesTheShellWithNoWayPast() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    setStatus(app, QStringLiteral("locked"));
    QCoreApplication::processEvents();

    QObject *g = gate(win);
    QVERIFY(g);
    QVERIFY(g->property("visible").toBool());
    QVERIFY(!app->storage()->ready());

    QQuickItem *field = findItem(win->contentItem(), "storagePassphrase");
    QVERIFY(field);
    QVERIFY(field->property("visible").toBool());

    QQuickItem *cancel = findItem(win->contentItem(), "storageGateCancel");
    QVERIFY2(!cancel || !cancel->property("visible").toBool(),
             "a locked store offered a way out of the gate");

    // The button waits for something to submit, then sends what was typed
    // under the name the Tcl reads.
    QQuickItem *go = findItem(win->contentItem(), "storageGateGo");
    QVERIFY(go);
    QVERIFY2(!go->property("enabled").toBool(),
             "Unlock was live with an empty passphrase");

    QSignalSpy sent(app->backend(), &TackyBackend::sent);
    g->setProperty("entry", QStringLiteral("open sesame"));
    QCoreApplication::processEvents();
    QVERIFY(go->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(go, "clicked"));

    QVERIFY(asked(sent, "storage", "unlock"));
    QCOMPARE(sent.at(0).at(2).toMap().value("passphrase").toString(),
             QString("open sesame"));
    e.assertNoErrors();
}

// A dialog that laid out to nothing still answers every findItem above, so the
// geometry is the part worth asserting.
void TestStorageUi::theGateIsRealEstateNotJustObjectNames() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    setStatus(app, QStringLiteral("locked"));
    QCoreApplication::processEvents();
    win->grabWindow();
    QCoreApplication::processEvents();

    QObject *g = gate(win);
    QVERIFY(g);
    const double gw = g->property("width").toDouble();
    const double gh = g->property("height").toDouble();
    QVERIFY2(gw > 100 && gh > 60,
             qPrintable(QStringLiteral("the gate laid out to %1x%2")
                            .arg(gw)
                            .arg(gh)));
    // And it fits the window it is centred in rather than hanging off it.
    QVERIFY(gw <= win->width());

    // Its contents are measured against the popup's own content item, which is
    // the item the column actually fills.
    auto *inner = g->property("contentItem").value<QQuickItem *>();
    QVERIFY(inner);
    QVERIFY(inner->width() > 100 && inner->height() > 60);

    // Every part of it is inside it, at distinct heights - a column that
    // collapsed would stack them all at zero.
    const QRectF bounds(0, 0, inner->width(), inner->height());
    QList<double> tops;
    for (const char *name : {"storageGateBlurb", "storagePassphrase",
                             "storageReveal", "storageGateGo"}) {
        QQuickItem *part = findItem(win->contentItem(), name);
        QVERIFY2(part, name);
        const QRectF r = itemRect(part, inner);
        QVERIFY2(r.height() > 0, name);
        QVERIFY2(bounds.contains(r),
                 qPrintable(QStringLiteral("%1 at %2,%3 %4x%5 is outside the gate's %6x%7")
                                .arg(QLatin1String(name))
                                .arg(r.x()).arg(r.y())
                                .arg(r.width()).arg(r.height())
                                .arg(inner->width()).arg(inner->height())));
        tops << r.y();
    }
    for (int i = 1; i < tops.size(); ++i)
        QVERIFY2(tops.at(i) > tops.at(i - 1), "the gate's rows share a row");
    e.assertNoErrors();
}

// tacky has no key escrow, so the warning is the whole of the informed part of
// choosing a passphrase - and this is the only screen that ever collects one
// for an encrypt.
void TestStorageUi::aPendingEncryptWarnsAndTakesAPassphrase() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    setStatus(app, QStringLiteral("pending-encrypt"));
    QCoreApplication::processEvents();

    QObject *g = gate(win);
    QVERIFY(g);
    QVERIFY(g->property("visible").toBool());

    QQuickItem *warn = findItem(win->contentItem(), "storageGateWarning");
    QVERIFY(warn);
    QVERIFY(warn->property("visible").toBool());
    QVERIFY(warn->property("text").toString().contains(
        QLatin1String("no way to recover")));

    QVERIFY(findItem(win->contentItem(), "storagePassphrase")
                ->property("visible")
                .toBool());
    // Staying unmigrated is always safe, so this gate can be abandoned.
    QQuickItem *cancel = findItem(win->contentItem(), "storageGateCancel");
    QVERIFY(cancel);
    QVERIFY(cancel->property("visible").toBool());

    QSignalSpy sent(app->backend(), &TackyBackend::sent);
    g->setProperty("entry", QStringLiteral("a new passphrase"));
    QVERIFY(QMetaObject::invokeMethod(findItem(win->contentItem(),
                                               "storageGateGo"),
                                      "clicked"));
    QVERIFY(asked(sent, "storage", "encrypt"));
    QCOMPARE(sent.at(0).at(2).toMap().value("passphrase").toString(),
             QString("a new passphrase"));

    // And Cancel drops the request rather than running it.
    sent.clear();
    QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
    QVERIFY(asked(sent, "storage", "cancelPending"));
    e.assertNoErrors();
}

// The passphrase for a decrypt is already in memory, from the unlock that
// revealed the request - asking again would be asking for a second one.
void TestStorageUi::aPendingDecryptAsksForNoPassphrase() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    setStatus(app, QStringLiteral("pending-decrypt"));
    QCoreApplication::processEvents();

    QObject *g = gate(win);
    QVERIFY(g);
    QVERIFY(g->property("visible").toBool());

    QQuickItem *field = findItem(win->contentItem(), "storagePassphrase");
    QVERIFY2(!field || !field->property("visible").toBool(),
             "the decrypt gate asked for a passphrase");
    QQuickItem *warn = findItem(win->contentItem(), "storageGateWarning");
    QVERIFY(!warn || !warn->property("visible").toBool());

    // Nothing to type, so the button is live from the start.
    QQuickItem *go = findItem(win->contentItem(), "storageGateGo");
    QVERIFY(go);
    QVERIFY(go->property("enabled").toBool());

    QSignalSpy sent(app->backend(), &TackyBackend::sent);
    QVERIFY(QMetaObject::invokeMethod(go, "clicked"));
    QVERIFY(asked(sent, "storage", "decrypt"));
    e.assertNoErrors();
}

// `storage unlock` is an ordinary call, not a respawn, so a wrong passphrase
// leaves the gate exactly where it was with tacky's own wording under the
// field.
void TestStorageUi::aRefusedPassphraseIsShownAndRetriedInPlace() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *win = loadMain(e);
    QVERIFY(win);

    setStatus(app, QStringLiteral("locked"));
    QCoreApplication::processEvents();

    QObject *g = gate(win);
    QVERIFY(g);
    g->setProperty("entry", QStringLiteral("wrong"));
    QQuickItem *go = findItem(win->contentItem(), "storageGateGo");
    QVERIFY(go);
    QVERIFY(QMetaObject::invokeMethod(go, "clicked"));

    // While the attempt is out there is nothing to press again.
    QVERIFY(app->storage()->busy());
    QVERIFY2(!go->property("enabled").toBool(),
             "the button stayed live while the attempt was out");

    // No interpreter here, so the refusal is the backend's own "not sent" -
    // which is the same path a wrong passphrase takes. What matters is that
    // whatever it said reaches the field; tst_storage covers the wording tacky
    // actually produces.
    QCoreApplication::processEvents();

    QQuickItem *err = findItem(win->contentItem(), "storageGateError");
    QVERIFY(err);
    QVERIFY(err->property("visible").toBool());
    QVERIFY(!err->property("text").toString().isEmpty());
    QCOMPARE(err->property("text").toString(), app->storage()->error());

    // Still gated, still asking, and live again for another go.
    QVERIFY(g->property("visible").toBool());
    QVERIFY(!app->storage()->busy());
    QVERIFY(go->property("enabled").toBool());

    // And a fresh attempt clears the message rather than leaving a stale one
    // under a retry that is still running.
    QVERIFY(QMetaObject::invokeMethod(go, "clicked"));
    QVERIFY(!err->property("visible").toBool());
    e.assertNoErrors();
}

void TestStorageUi::theCardNamesTheStateAndTheOneActionForIt() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QScopedPointer<QObject> holder;
    QQuickWindow *win = openPreferences(e, holder);
    QVERIFY(win);

    struct Case {
        const char *status;
        const char *button;
        bool encrypted;
    };
    const Case cases[] = {
        {"plaintext", "Encrypt local storage…", false},
        {"unlocked", "Remove encryption…", true},
        {"pending-encrypt", "Cancel pending encryption", false},
        {"pending-decrypt", "Cancel pending removal", true}};

    for (const Case &c : cases) {
        setStatus(app, QString::fromLatin1(c.status));
        QCoreApplication::processEvents();

        QQuickItem *action = findItem(win->contentItem(),
                                      "storageActionButton");
        QVERIFY2(action, c.status);
        QVERIFY2(action->property("visible").toBool(), c.status);
        QCOMPARE(action->property("text").toString(),
                 QString::fromUtf8(c.button));

        // The status line says whether the store is encrypted *now*, which a
        // pending request has not changed yet.
        QQuickItem *line = findItem(win->contentItem(), "storageStatus");
        QVERIFY2(line, c.status);
        const QString text = line->property("text").toString();
        QVERIFY2(text.contains(QLatin1String("are encrypted")) == c.encrypted,
                 qPrintable(QStringLiteral("%1 said: %2")
                                .arg(QLatin1String(c.status), text)));
    }
    e.assertNoErrors();
}

// Losing the passphrase loses the data, so this is asked about twice - and the
// card only ever requests the migration; the gate is what runs it.
void TestStorageUi::encryptingIsConfirmedBeforeItIsRequested() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QScopedPointer<QObject> holder;
    QQuickWindow *win = openPreferences(e, holder);
    QVERIFY(win);

    setStatus(app, QStringLiteral("plaintext"));
    QCoreApplication::processEvents();

    QSignalSpy sent(app->backend(), &TackyBackend::sent);
    QVERIFY(QMetaObject::invokeMethod(
        findItem(win->contentItem(), "storageActionButton"), "clicked"));
    QCoreApplication::processEvents();

    QVERIFY2(!asked(sent, "storage", "requestEncrypt"),
             "the card asked for encryption without confirming");

    auto *confirm = holder->findChild<QObject *>(QStringLiteral("storageConfirm"));
    QVERIFY(confirm);
    QVERIFY(confirm->property("visible").toBool());
    QVERIFY(confirm->property("message").toString().contains(
        QLatin1String("no way to recover")));

    // In the window it was asked from, not whichever window came first.
    // Preferences is its own window on desktop, and findChild finds the dialog
    // by declaration parentage whichever window it actually draws in - so the
    // question worth asking is where it landed.
    auto *over = confirm->property("parent").value<QQuickItem *>();
    QVERIFY2(over, "the confirmation has no parent item to draw in");
    QCOMPARE(over->window(), win);

    QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
    QCoreApplication::processEvents();
    QVERIFY(asked(sent, "storage", "requestEncrypt"));
    e.assertNoErrors();
}

// Preferences is its own window on desktop, opened from a shell that is
// already up - and a dialog has to appear over the window that asked for it.
// The shell carries a second AppSettingsPage of its own for the mobile sheet,
// so "a storageConfirm exists and is visible" is true of the wrong one too;
// what matters is which window it drew in.
void TestStorageUi::theConfirmationOpensOverPreferencesNotTheShell() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QQuickWindow *shell = loadMain(e);
    QVERIFY(shell);

    auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
    QVERIFY(mgr);
    QVariant opened;
    QVERIFY(QMetaObject::invokeMethod(mgr, "preferences",
                                      Q_RETURN_ARG(QVariant, opened)));
    auto *prefs = qobject_cast<QQuickWindow *>(opened.value<QObject *>());
    QVERIFY(prefs);
    QVERIFY(QTest::qWaitForWindowExposed(prefs));
    prefs->grabWindow();
    QCoreApplication::processEvents();

    setStatus(app, QStringLiteral("plaintext"));
    QCoreApplication::processEvents();

    QQuickItem *action = findItem(prefs->contentItem(), "storageActionButton");
    QVERIFY(action);
    QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
    QCoreApplication::processEvents();

    auto *confirm = prefs->findChild<QObject *>(QStringLiteral("storageConfirm"));
    QVERIFY(confirm);
    QVERIFY(confirm->property("visible").toBool());

    auto *over = confirm->property("parent").value<QQuickItem *>();
    QVERIFY2(over, "the confirmation has no parent item to draw in");
    QVERIFY2(over->window() != shell,
             "the confirmation opened over the shell instead of Preferences");
    QCOMPARE(over->window(), prefs);

    QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
    QCoreApplication::processEvents();
    // And the shell's own copy of the page was never involved.
    auto *shellConfirm =
        shell->findChild<QObject *>(QStringLiteral("storageConfirm"));
    QVERIFY2(!shellConfirm || !shellConfirm->property("visible").toBool(),
             "the shell's hidden settings page opened a dialog of its own");

    // The request is for the next launch, so nothing asks for a passphrase
    // now - not over Preferences and not over the shell. Requesting one is
    // the only way to reach pending-* from a running app, and the migration
    // may only run before any account has connected.
    setStatus(app, QStringLiteral("pending-encrypt"));
    QCoreApplication::processEvents();
    for (QQuickWindow *w : {shell, prefs}) {
        QObject *g = gate(w);
        QVERIFY2(!g || !g->property("visible").toBool(),
                 "asking to encrypt put the passphrase prompt up mid-session");
    }
    QVERIFY(!app->storage()->gateActive());

    QVERIFY(QMetaObject::invokeMethod(prefs, "close"));
    QCoreApplication::processEvents();
    e.assertNoErrors();
}

// Nothing has happened on disk yet, so taking the request back needs no
// warning of its own.
void TestStorageUi::aPendingRequestIsCancelledStraightFromTheCard() {
    AppEngine e;
    auto *app = e.singletonInstance<AppController *>("Quack", "App");
    QVERIFY(app);
    QScopedPointer<QObject> holder;
    QQuickWindow *win = openPreferences(e, holder);
    QVERIFY(win);

    setStatus(app, QStringLiteral("pending-encrypt"));
    QCoreApplication::processEvents();

    QSignalSpy sent(app->backend(), &TackyBackend::sent);
    QVERIFY(QMetaObject::invokeMethod(
        findItem(win->contentItem(), "storageActionButton"), "clicked"));
    QCoreApplication::processEvents();

    QVERIFY(asked(sent, "storage", "cancelPending"));
    auto *confirm = holder->findChild<QObject *>(QStringLiteral("storageConfirm"));
    QVERIFY2(!confirm || !confirm->property("visible").toBool(),
             "taking back a request that changed nothing asked for confirmation");
    e.assertNoErrors();
}

QTEST_MAIN(TestStorageUi)
#include "tst_qmlload_storage.moc"
