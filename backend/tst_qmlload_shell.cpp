// The app's own windows: the shell at either width, the account drawer and the
// rail it carries, the backend notice, and the call windows a row brings up.
#include <QtTest>
#include <QJsonDocument>
#include <QQmlComponent>

#include "AppController.h"
#include "CallsModel.h"
#include "ChatListModel.h"
#include "TackyTransport.h"

#include "QmlTestSupport.h"

using namespace qmltest;

namespace {
// A transport that goes up and down on command and speaks to nobody. The UI
// tests otherwise run with no transport at all, which is a different state
// from one that dropped.
class FakeTransport : public TackyTransport {
    Q_OBJECT
public:
    bool start(const QStringList &) override {
        setConnected(true);
        return true;
    }
    void stop() override { setConnected(false); }
    bool isConnected() const override { return m_connected; }
    void send(const QByteArray &) override {}

    void deliver(const QByteArray &json) { emit received(QString::fromUtf8(json)); }

private:
    void setConnected(bool on) {
        if (m_connected == on)
            return;
        m_connected = on;
        emit connectedChanged();
    }
    bool m_connected = false;
};
} // namespace

class TestShell : public QObject {
    Q_OBJECT

    // The app's own window, loaded the way main() loads it.
    static QQuickWindow *loadMain(AppEngine &e) {
        e.loadFromModule("Quack", "Main");
        if (e.rootObjects().isEmpty())
            return nullptr;
        return qobject_cast<QQuickWindow *>(e.rootObjects().first());
    }

    // Both enabled: a disabled account says so instead of saying what its
    // connection is doing, which is not what the rail tests are about.
    static void seedTwoAccounts(AppController *app) {
        app->accounts()->applyList({"me@example.com", "alt@example.com"});
        app->accounts()->applyEnabledList({"me@example.com", "alt@example.com"});
        app->accounts()->setConnState("me@example.com", "connected");
        app->accounts()->setConnState("alt@example.com", "conn-error");
    }

    // An AppShell filling `win`, held open by `holder`.
    static QQuickItem *openShell(QQmlEngine &e, QQuickWindow &win,
                                 QScopedPointer<QObject> &holder) {
        win.show();
        if (!QTest::qWaitForWindowExposed(&win))
            return nullptr;
        QQmlComponent comp(&e, "Quack", "AppShell");
        if (!comp.isReady()) {
            qWarning("%s", qPrintable(comp.errorString()));
            return nullptr;
        }
        holder.reset(comp.createWithInitialProperties(
            {{"initialAccount", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        auto *shell = qobject_cast<QQuickItem *>(holder.data());
        if (shell)
            shell->setParentItem(win.contentItem());
        return shell;
    }

    // The rail lives on the window overlay rather than among the shell's own
    // items, so it is the QObject tree that finds the drawer holding it.
    static QObject *drawerOf(QQuickItem *shell) {
        return shell->findChild<QObject *>("accountDrawer");
    }

private slots:
    // The app's own startup, then quit with a settings window open - which
    // segfaulted until AppWindows took its own windows down first. The app does
    // that from Qt.application.aboutToQuit, which no test can raise without
    // ending the whole run, so this drives closeAll() directly.
    void closingTheWindowsItHoldsSurvivesShutdown() {
        AppEngine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QVERIFY(loadMain(e));

        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant settings;
        QVERIFY(QMetaObject::invokeMethod(mgr, "accountSettings",
                                          Q_RETURN_ARG(QVariant, settings),
                                          Q_ARG(QVariant, QVariant("me@example.com"))));
        QVERIFY(settings.value<QObject *>());
        QCoreApplication::processEvents();

        QPointer<QObject> held(settings.value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(mgr, "closeAll"));
        QCoreApplication::processEvents();
        QTRY_VERIFY2(held.isNull(), "the settings window outlived closeAll()");

        e.assertNoErrors();
    }

    // Driven as keys rather than by calling what they are bound to: a Shortcut
    // the key never reaches, or misses because Shift turns Tab into Backtab,
    // leaves every other test here green.
    void ctrlTabWalksTheConversationList() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","last_activity":300},
            {"jid":"bob@example.com","name":"Bob","last_activity":200},
            {"jid":"room@muc.example.com?join","name":"Room","groupchat":true,
             "last_activity":100}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow *win = loadMain(e);
        QVERIFY(win);
        QVERIFY(QTest::qWaitForWindowExposed(win));
        win->requestActivate();
        QVERIFY2(QTest::qWaitForWindowActive(win),
                 "the window never took focus, so no shortcut could fire");

        auto *shell = win->findChild<QQuickItem *>("appShell");
        QVERIFY(shell);
        // Named rather than left to the fallback, which takes the first account
        // there is - alt@, whose list is empty.
        shell->setProperty("currentAccount", "me@example.com");
        auto jid = [&] { return shell->property("currentChatJid").toString(); };
        QCOMPARE(jid(), QString());

        QTest::keyClick(win, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(jid(), QString("amy@example.com"));
        QTest::keyClick(win, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(jid(), QString("bob@example.com"));

        // The row's kind travels with it: a room opened as a 1:1 chat talks to
        // the JID instead of joining it.
        QTest::keyClick(win, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(jid(), QString("room@muc.example.com?join"));
        QCOMPARE(shell->property("currentChatName").toString(), QString("Room"));
        QVERIFY(shell->property("currentChatGroupchat").toBool());

        // Off the foot, round to the head.
        QTest::keyClick(win, Qt::Key_Tab, Qt::ControlModifier);
        QTRY_COMPARE(jid(), QString("amy@example.com"));

        // Backward, as a real keyboard sends it: Shift+Tab arrives as Backtab,
        // which is what the second sequence is registered for.
        QTest::keyClick(win, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(jid(), QString("room@muc.example.com?join"));
        QTest::keyClick(win, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(jid(), QString("bob@example.com"));

        e.assertNoErrors();
    }

    // The notice is the only thing that says a shared backend went away: every
    // model filters events by its own module and drops the rest.
    void backendNoticeFollowsTheBackendGoingAway() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *fake = new FakeTransport;
        app->backend()->setTransport(fake);
        QQuickWindow *win = loadMain(e);
        QVERIFY(win);
        auto *notice = win->findChild<QQuickItem *>("backendNotice");
        QVERIFY(notice);

        // Attached but not up yet: that is a backend to wait for.
        QVERIFY(notice->isVisible());
        QVERIFY(notice->property("message").toString().contains("Reconnecting"));

        fake->start({});
        QTRY_VERIFY(!notice->isVisible());

        fake->stop();
        QTRY_VERIFY(notice->isVisible());
        e.assertNoErrors();
    }

    // A UI with no transport has no backend to miss, which is what keeps the
    // notice off every other test in this file.
    void backendNoticeStaysOffWithNoTransport() {
        AppEngine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QQuickWindow *win = loadMain(e);
        QVERIFY(win);
        auto *notice = win->findChild<QQuickItem *>("backendNotice");
        QVERIFY(notice);
        QVERIFY(!notice->isVisible());
        e.assertNoErrors();
    }

    // error <Background> reaches the user as its message alone: errorinfo is a
    // Tcl trace, which the backend has already logged.
    void backendNoticeShowsAReportedFailure() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *fake = new FakeTransport;
        app->backend()->setTransport(fake);
        fake->start({});
        QQuickWindow *win = loadMain(e);
        QVERIFY(win);
        auto *notice = win->findChild<QQuickItem *>("backendNotice");
        QVERIFY(notice);
        QVERIFY(!notice->isVisible());

        fake->deliver(R"(["event","error","Background",)"
                      R"({"message":"cannot read x","errorinfo":"trace\n at y"}])");
        QTRY_VERIFY(notice->isVisible());
        const QString shown = notice->property("message").toString();
        QCOMPARE(shown, QString("cannot read x"));
        e.assertNoErrors();
    }

    void loadsApp() {
        AppEngine e;
        // create the App singleton up front; backend stays unstarted
        e.singletonInstance<AppController *>("Quack", "App");
        QQuickWindow *win = loadMain(e);
        QVERIFY(win);
        win->grabWindow(); // force a render so lazy bindings evaluate
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }

    void loadsMultiWindow() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

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

        e.assertNoErrors();
    }

    // Controls the app has not drawn itself - the search entry among them - are
    // drawn by the style out of the window's palette. Left at the system's,
    // that entry came out in the desktop's colours in the middle of a themed
    // app. The field's own palette is what is checked rather than the colours
    // its background ended up with: whether a style reads a role is the style's
    // business (Fusion builds the whole field out of them, Material paints from
    // its attached properties instead), while what this window owes every one
    // of them is the theme.
    void theWindowsPaletteReachesTheSearchEntry() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        seedTwoAccounts(app); // the entry is disabled until there is an account

        QQmlComponent comp(&e, "Quack", "ShellWindow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> holder(comp.createWithInitialProperties(
            {{"initialAccount", "me@example.com"}}));
        auto *win = qobject_cast<QQuickWindow *>(holder.data());
        QVERIFY(win);
        QVERIFY(QTest::qWaitForWindowExposed(win));

        QQuickItem *field = findItem(win->contentItem(), "searchField");
        QVERIFY(field);
        QVERIFY(field->property("enabled").toBool());
        auto *pal = field->property("palette").value<QObject *>();
        QVERIFY(pal);
        QCOMPARE(pal->property("base").value<QColor>(),
                 theme->property("field").value<QColor>());
        QCOMPARE(pal->property("text").value<QColor>(),
                 theme->property("textPrimary").value<QColor>());
        QCOMPARE(pal->property("placeholderText").value<QColor>(),
                 theme->property("textDim").value<QColor>());
        QCOMPARE(pal->property("highlight").value<QColor>(),
                 theme->property("accent").value<QColor>());

        e.assertNoErrors();
    }

    // Call windows are never asked for: they follow a CallsModel row, with the
    // roles arriving as the delegate's required properties. This is the part
    // that no C++ test can reach, so drive canned events through the real
    // singleton and look at what is actually on screen.
    //
    // A ringing call is the dialog's, not the call window's - the window only
    // appears once there is a call in it, as tacky's Tk GUI does it.
    void ringingCallShowsTheDialogUntilItIsAnswered() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QVERIFY(loadMain(e)); // ShellWindow arms AppWindows

        const int before = visibleWindows().size();
        app->calls()->handleEvent(
            "calls", "Incoming",
            QVariantMap{{"acc", "me@example.com"},
                        {"sid", "tk-qml"},
                        {"from", "friend@example.com"}});
        QCoreApplication::processEvents();

        QVERIFY2(visibleWindowTitled("Incoming Call"),
                 "no dialog appeared for the ringing call");
        QVERIFY2(!visibleWindowTitled("Call —"),
                 "a call window appeared for a call still ringing");
        QCOMPARE(visibleWindows().size(), before + 1);

        // Answering swaps one for the other: the dialog has nothing left to
        // ask, and the call is now a call.
        app->calls()->accept("me@example.com", "tk-qml");
        QCoreApplication::processEvents();
        QVERIFY(!visibleWindowTitled("Incoming Call"));
        QWindow *call = visibleWindowTitled("friend@example.com");
        QVERIFY2(call, "answering did not raise the call window");
        QCOMPARE(visibleWindows().size(), before + 1);

        // Dismissing the row is what closes it - the window owns no lifetime
        // of its own.
        app->calls()->dismiss("me@example.com", "tk-qml");
        QCoreApplication::processEvents();
        QTRY_COMPARE(visibleWindows().size(), before);

        e.assertNoErrors();
    }

    // Declining never shows a call window, so nothing is left holding the row
    // open: it has to clear itself or it sits in the model unseen forever.
    void decliningARingingCallClearsTheRow() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QVERIFY(loadMain(e));

        const int before = visibleWindows().size();
        CallsModel *calls = app->calls();
        calls->handleEvent("calls", "Incoming",
                           QVariantMap{{"acc", "me@example.com"},
                                       {"sid", "tk-decline"},
                                       {"from", "friend@example.com"}});
        QCoreApplication::processEvents();
        QVERIFY(visibleWindowTitled("Incoming Call"));

        calls->reject("me@example.com", "tk-decline");
        QCoreApplication::processEvents();
        QVERIFY2(!visibleWindowTitled("Call —"),
                 "declining raised a call window on the way out");
        QTRY_COMPARE(calls->rowCount(), 0);
        QTRY_COMPARE(visibleWindows().size(), before);

        e.assertNoErrors();
    }

    // Closing the window is not a way to walk out on a running call: it hangs
    // up, and only then does the row (and with it the window) go.
    void closingACallWindowHangsUp() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QVERIFY(loadMain(e));

        CallsModel *calls = app->calls();
        calls->handleEvent("calls", "Active",
                           QVariantMap{}); // no sid: must be ignored outright
        calls->handleEvent("calls", "Outgoing",
                           QVariantMap{{"acc", "me@example.com"},
                                       {"sid", "tk-close"},
                                       {"to", "friend@example.com"}});
        QCoreApplication::processEvents();

        QWindow *call = visibleWindowTitled("friend@example.com");
        QVERIFY2(call, "a call we placed did not take the call window");

        call->close();
        QCoreApplication::processEvents();
        QCOMPARE(calls->rowCount(), 1);
        QCOMPARE(calls->data(calls->index(0), CallsModel::StateRole).toString(),
                 QString("ended"));
        // The window's own timer clears the finished row shortly after.
        QTRY_COMPARE_WITH_TIMEOUT(calls->rowCount(), 0, 4000);

        e.assertNoErrors();
    }

    // Calling one of your own accounts from another is one session but two
    // calls, and this app is on both ends of it. What is on screen is one call
    // window and one ringing dialog - never two call windows on the same spot,
    // which is unreadable - and, the bug this pins, one end finishing says
    // nothing about the other: a sibling device answering ends the callee end
    // while the caller end is up and audible.
    void bothEndsOfACallBetweenOwnAccountsShowOneWindowAndOneDialog() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QVERIFY(loadMain(e));

        const int before = visibleWindows().size();
        CallsModel *calls = app->calls();
        calls->handleEvent(
            "calls", "Outgoing",
            QVariantMap{{"acc", "a@host"}, {"sid", "tk-s"}, {"to", "b@host"}});
        calls->handleEvent(
            "calls", "Incoming",
            QVariantMap{{"acc", "b@host"}, {"sid", "tk-s"}, {"from", "a@host"}});
        QCoreApplication::processEvents();

        // One sid, two rows. The end that placed the call holds the window; the
        // end being rung holds the dialog.
        QCOMPARE(calls->rowCount(), 2);
        QWindow *window = visibleWindowTitled("Call —");
        QVERIFY2(window, "no call window for the calling end");
        QCOMPARE(window->title(), QStringLiteral("Call — b@host"));
        QVERIFY2(visibleWindowTitled("Incoming Call"),
                 "no dialog for the end being rung");
        QCOMPARE(visibleWindows().size(), before + 2);

        // The answering end goes away (another device took it). The calling end
        // must not follow it out - that call is still running.
        calls->handleEvent("calls", "Ended",
                           QVariantMap{{"acc", "b@host"}, {"sid", "tk-s"}});
        calls->handleEvent("calls", "Active",
                           QVariantMap{{"acc", "a@host"}, {"sid", "tk-s"}});
        QCoreApplication::processEvents();
        QTRY_COMPARE(calls->rowCount(), 1);
        QCOMPARE(calls->data(calls->index(0), CallsModel::StateRole).toString(),
                 QString("active"));
        QVERIFY2(visibleWindowTitled("Call — b@host"),
                 "the live call's window was closed");
        QVERIFY(!visibleWindowTitled("Incoming Call"));

        calls->dismiss("a@host", "tk-s");
        QCoreApplication::processEvents();
        QTRY_COMPARE(visibleWindows().size(), before);

        e.assertNoErrors();
    }

    // The account rail is a pull-out drawer at every width, never a column: the
    // list header's badge is the way in and back is the way out, at 400px and at
    // 1000px alike.
    // No rail among the shell's own items at either width: the only copy is the
    // drawer's, and the list header holds the way to it.
    void theRailIsOnlyEverTheDrawersCopy() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QVERIFY(!shell->property("wide").toBool());

        QVERIFY2(!findItem(shell, "accountRail"), "the rail is back in the layout");
        QQuickItem *accountsBtn = findItem(shell, "accountsButton");
        QVERIFY(accountsBtn);
        QVERIFY(accountsBtn->isVisible());

        e.assertNoErrors();
    }

    // The header button wears the current account, and its dot follows the
    // connection - with the rail shut nothing else shows it.
    void theAccountsButtonWearsTheConnection() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);

        QQuickItem *badge = findItem(shell, "accountStatusBadge");
        QVERIFY(badge);
        QCOMPARE(badge->property("jid").toString(), QString("me@example.com"));
        QCOMPARE(badge->property("stateColor").value<QColor>(),
                 theme->property("positive").value<QColor>());
        app->accounts()->setConnState("me@example.com", "auth-error");
        QTRY_COMPARE(badge->property("stateColor").value<QColor>(),
                     theme->property("negative").value<QColor>());

        e.assertNoErrors();
    }

    void theHeaderButtonPullsTheDrawerOut() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);
        QVERIFY(!drawer->property("opened").toBool());

        QQuickItem *accountsBtn = findItem(shell, "accountsButton");
        QVERIFY(accountsBtn);
        const QPoint tap = win.contentItem()
                               ->mapFromItem(accountsBtn,
                                             QPointF(accountsBtn->width() / 2,
                                                     accountsBtn->height() / 2))
                               .toPoint();
        QTest::mouseClick(&win, Qt::LeftButton, Qt::NoModifier, tap);
        QTRY_VERIFY2(drawer->property("opened").toBool(),
                     "the header button did not pull the drawer out");

        auto *drawerRail = drawer->findChild<QQuickItem *>("accountRail");
        QVERIFY(drawerRail);
        // Wide enough to spell the accounts out, which is the whole reason the
        // rail is a drawer rather than a strip of avatars.
        QVERIFY2(drawerRail->width() > 200,
                 qPrintable(QString("drawer rail is only %1 wide")
                                .arg(drawerRail->width())));

        e.assertNoErrors();
    }

    // A Drawer sizes to its content, and this rail is built from anchors, so it
    // offers no implicit height. Left alone the drawer opens zero-height: the
    // list collapses, the column overflows it, and the ＋ button is the only
    // thing drawn - over nothing, the background having no height either. Model
    // counts read correct straight through that, so geometry is what has to be
    // asserted.
    void theDrawerRailFillsTheHeightItIsGiven() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);
        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());

        auto *drawerRail = drawer->findChild<QQuickItem *>("accountRail");
        QVERIFY(drawerRail);
        QCOMPARE(drawerRail->height(), qreal(win.height()));
        QQuickItem *rows = drawerRail->findChild<QQuickItem *>("accountRailList");
        QVERIFY(rows);
        QVERIFY2(rows->height() > 0, "the account rows collapsed to nothing");
        QQuickItem *addBtn = drawerRail->findChild<QQuickItem *>("addAccountButton");
        QVERIFY(addBtn);
        QVERIFY2(addBtn->y() + addBtn->height() <= drawerRail->height(),
                 qPrintable(QString("＋ spills out: ends at %1, rail is %2 tall")
                                .arg(addBtn->y() + addBtn->height())
                                .arg(drawerRail->height())));

        // Both rows are laid out, not collapsed onto each other at the origin.
        const auto rowJids = findItems(drawerRail, "accountRowJid");
        QCOMPARE(rowJids.size(), 2);
        for (QQuickItem *jid : rowJids) {
            QVERIFY(jid->height() > 0);
            QVERIFY(jid->width() > 0);
        }
        const qreal firstY = rowJids.at(0)->mapToItem(drawerRail, QPointF(0, 0)).y();
        const qreal secondY = rowJids.at(1)->mapToItem(drawerRail, QPointF(0, 0)).y();
        QVERIFY(qAbs(secondY - firstY) >= rowJids.at(0)->height());

        e.assertNoErrors();
    }

    // In words, not just the dot's colour, which is red for both a rejected
    // password and an unreachable server.
    void theRailSaysWhatEachConnectionIsDoing() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);
        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *drawerRail = drawer->findChild<QQuickItem *>("accountRail");
        QVERIFY(drawerRail);

        QStringList states;
        const auto stateItems = findItems(drawerRail, "accountRowState");
        for (QQuickItem *item : stateItems)
            states << item->property("text").toString();
        std::sort(states.begin(), states.end());
        QCOMPARE(states, QStringList({"connected", "connection failed"}));

        e.assertNoErrors();
    }

    // Android's back closes the drawer before it touches the navigation
    // underneath it, and picking an account - the whole errand - closes it too.
    void theDrawerClosesOnBackAndOnAPick() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);
        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());

        QVariant popped;
        QVERIFY(QMetaObject::invokeMethod(shell, "handleBack",
                                          Q_RETURN_ARG(QVariant, popped)));
        QVERIFY(popped.toBool());
        QTRY_VERIFY(!drawer->property("opened").toBool());

        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto *drawerRail = drawer->findChild<QQuickItem *>("accountRail");
        QVERIFY(drawerRail);
        QVERIFY(QMetaObject::invokeMethod(drawerRail, "selectAccount",
                                          Q_ARG(QString, QString("alt@example.com"))));
        QCOMPARE(shell->property("currentAccount").toString(),
                 QString("alt@example.com"));
        QTRY_VERIFY(!drawer->property("opened").toBool());

        e.assertNoErrors();
    }

    // The header button is not the only way in; a drag from the left edge pulls
    // it out too. Whether Android's own edge gesture lets that touch through
    // before claiming it for Back is a system question no offscreen test can
    // answer - this pins the Qt half only.
    void anEdgeDragPullsTheDrawerOutUnlessAChatIsOpen() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);
        QVERIFY(drawer->property("interactive").toBool());

        QTest::mousePress(&win, Qt::LeftButton, Qt::NoModifier, QPoint(2, 400));
        for (int x = 10; x <= 260; x += 10)
            QTest::mouseMove(&win, QPoint(x, 400));
        QTest::mouseRelease(&win, Qt::LeftButton, Qt::NoModifier, QPoint(260, 400));
        QTRY_VERIFY2(drawer->property("opened").toBool(),
                     "an edge drag did not pull the drawer out");
        QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
        QTRY_VERIFY(!drawer->property("opened").toBool());

        // Over an open chat that edge belongs to going back to the list, not to
        // a rail the chat has no room for.
        shell->setProperty("currentChatJid", "friend@example.com");
        QVERIFY(!drawer->property("interactive").toBool());
        QVERIFY(QMetaObject::invokeMethod(shell, "closeChat"));

        e.assertNoErrors();
    }

    // Wide changes nothing about the rail: same drawer, same button, the list
    // still starting at the window's edge with no column in front of it, and
    // crossing the breakpoint leaves an open drawer be - there is no second
    // copy of the rail for it to double.
    void theDrawerIsTheRailAtEveryWidth() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        seedTwoAccounts(app);

        QQuickWindow win;
        win.resize(400, 700);
        QScopedPointer<QObject> holder;
        QQuickItem *shell = openShell(e, win, holder);
        QVERIFY(shell);
        QObject *drawer = drawerOf(shell);
        QVERIFY(drawer);

        shell->setWidth(1000);
        QTRY_VERIFY(shell->property("wide").toBool());
        QQuickItem *accountsBtn = findItem(shell, "accountsButton");
        QVERIFY(accountsBtn);
        QVERIFY(accountsBtn->isVisible());
        QQuickItem *list = findItem(shell, "conversationsPane");
        QVERIFY(list);
        QTRY_COMPARE(list->x(), qreal(0));

        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        shell->setWidth(400);
        QTRY_VERIFY(!shell->property("wide").toBool());
        QVERIFY(drawer->property("opened").toBool());
        shell->setWidth(1000);
        QTRY_VERIFY(shell->property("wide").toBool());
        QVERIFY(drawer->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
        QTRY_VERIFY(!drawer->property("opened").toBool());

        win.grabWindow();
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }

    // Android kills the UI process while the service keeps the accounts online.
    // On reopen the account list comes back before anything about those
    // accounts does, and a rail that reads the blanks as facts walks a
    // connected account through "disabled" and "offline" on the way in.
    void theRailSaysNothingUntilItHasBeenTold() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);

        app->accounts()->applyList({"me@example.com"});

        QQuickWindow win;
        win.resize(360, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AccountRail");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"width", win.width()}, {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *rail = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(rail);
        rail->setParentItem(win.contentItem());

        const auto states = findItems(rail, "accountRowState");
        QCOMPARE(states.size(), 1);
        QQuickItem *badge = findItem(rail, "accountRowBadge");
        QVERIFY(badge);
        QCOMPARE(states.at(0)->property("text").toString(), QString("checking…"));
        QVERIFY(!badge->property("statusKnown").toBool());
        // No dimming either: 45% opacity is how the rail says "disabled".
        QQuickItem *avatar = findItem(badge, "accountBadgeAvatar");
        QVERIFY(avatar);
        QCOMPARE(avatar->opacity(), qreal(1.0));

        // The enabled subset on its own still leaves the connection open.
        app->accounts()->applyEnabledList({"me@example.com"});
        QCoreApplication::processEvents();
        QCOMPARE(states.at(0)->property("text").toString(), QString("checking…"));
        QCOMPARE(badge->property("stateColor").value<QColor>(),
                 theme->property("textDim").value<QColor>());

        app->accounts()->setConnState("me@example.com", "connected");
        QTRY_COMPARE(states.at(0)->property("text").toString(), QString("connected"));
        QTRY_COMPARE(badge->property("stateColor").value<QColor>(),
                     theme->property("positive").value<QColor>());

        // A disabled account waits for nothing: no conn event is coming for it.
        app->accounts()->applyAdded("off@example.com");
        app->accounts()->applyEnabledList({"me@example.com"});
        QTRY_COMPARE(findItems(rail, "accountRowState").size(), 2);
        QStringList words;
        for (QQuickItem *item : findItems(rail, "accountRowState"))
            words << item->property("text").toString();
        std::sort(words.begin(), words.end());
        QCOMPARE(words, QStringList({"connected", "disabled"}));

        e.assertNoErrors();
    }

    // Stacked, opening a chat is a push, not a swap: the chat comes in from
    // the right edge over the list, and the list only drops out once it lands.
    // Both panes are on screen at full width for the length of that, which no
    // two panes of a split can be - hence the shell placing them itself.
    void narrowLayoutPushesTheChatOverTheList() {
        constexpr int kNarrow = 400; // one column, under the 720 breakpoint
        constexpr int kWide = 1000;  // rail, list and chat side by side

        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        app->accounts()->applyList({"me@example.com"});

        // Sized for the widest the shell gets: the divider drag at the end is a
        // real press, and presses land in window coordinates.
        QQuickWindow win;
        win.resize(kWide, 700);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AppShell");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"initialAccount", "me@example.com"},
             {"width", kNarrow},
             {"height", 700}}));
        auto *shell = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(shell);
        shell->setParentItem(win.contentItem());
        QVERIFY(!shell->property("wide").toBool());

        QQuickItem *list = findItem(shell, "conversationsPane");
        QQuickItem *chat = findItem(shell, "chatPane");
        QVERIFY(list);
        QVERIFY(chat);
        // Nothing open: the list has the column to itself.
        QVERIFY(!chat->isVisible());
        QTRY_COMPARE(list->width(), qreal(kNarrow));

        shell->setProperty("currentChatName", "Friend");
        shell->setProperty("currentChatJid", "friend@example.com");
        // Up at full width and still out at the right edge with the list under
        // it: the state a swap never passes through.
        QVERIFY2(chat->isVisible(), "the chat was still hidden when the push began");
        QCOMPARE(chat->width(), qreal(kNarrow));
        QVERIFY2(chat->x() >= qreal(kNarrow),
                 qPrintable(QString("the chat opened already home, at x=%1")
                                .arg(chat->x())));
        QVERIFY2(list->isVisible(), "the list vanished out from under the push");
        QTRY_COMPARE(chat->x(), qreal(0));
        QTRY_VERIFY2(!list->isVisible(), "the list stayed up behind a landed chat");

        // The shell holds the chat until the pop lands. Drop it when the press
        // arrives instead and it is a blank pane that slides off.
        QVERIFY(QMetaObject::invokeMethod(shell, "closeChat"));
        QCOMPARE(shell->property("currentChatJid").toString(),
                 QString("friend@example.com"));
        QCOMPARE(chat->property("chatName").toString(), QString("Friend"));
        QVERIFY(chat->isVisible());
        QVERIFY(list->isVisible());
        QTRY_VERIFY2(!chat->isVisible(), "the chat never finished sliding off");
        QCOMPARE(chat->x(), qreal(kNarrow));
        QCOMPARE(shell->property("currentChatJid").toString(), QString());

        // Wide there is no push: the chat is a column again, running from the
        // divider to the far edge.
        shell->setWidth(kWide);
        QTRY_VERIFY(shell->property("wide").toBool());
        QVERIFY(chat->isVisible());
        QVERIFY(list->isVisible());
        // The panes re-lay out on the next polish; let it settle before reading
        // where they meet.
        QTRY_COMPARE(chat->x() + chat->width(), shell->width());
        const qreal seam = list->mapToItem(shell, QPointF(list->width(), 0)).x();
        QVERIFY2(qAbs(chat->x() - seam) <= 2,
                 qPrintable(QString("the chat starts at %1, the list runs to %2")
                                .arg(chat->x())
                                .arg(seam)));

        // The divider sits above both columns, so a press on the seam reaches it
        // rather than the chat's leading edge.
        const qreal before = list->width();
        const QPoint grab(qRound(seam), 300);
        QTest::mousePress(&win, Qt::LeftButton, Qt::NoModifier, grab);
        for (int dx = 5; dx <= 80; dx += 5)
            QTest::mouseMove(&win, grab + QPoint(dx, 0));
        QTest::mouseRelease(&win, Qt::LeftButton, Qt::NoModifier, grab + QPoint(80, 0));
        QTRY_VERIFY2(list->width() > before + 40,
                     qPrintable(QString("the divider did not take the drag: the "
                                        "list is %1 wide, was %2")
                                    .arg(list->width())
                                    .arg(before)));

        win.grabWindow();
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }

    // The rail slides over the chat list, so the two fills have to be different
    // paint. They cannot be told apart at all if a palette hands both the same
    // value.
    void theRailNeverSharesAFillWithTheChatList() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palettes = theme->property("palettes").toMap();
        for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
            const QVariantMap palette = it.value().toMap();
            const QColor rail = QColor::fromString(palette.value("rail").toString());
            const QColor surface = QColor::fromString(palette.value("surface").toString());
            QVERIFY2(rail.isValid(), qPrintable(it.key() + " has no rail colour"));
            QVERIFY2(rail != surface,
                     qPrintable(QStringLiteral("palette \"%1\" paints the rail and the "
                                               "chat list the same %2")
                                    .arg(it.key(), rail.name())));
        }
    }
};

QTEST_MAIN(TestShell)
#include "tst_qmlload_shell.moc"
