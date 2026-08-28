// The conversations list: what a row wears, what a tap does, and the menu it
// opens.
#include <QtTest>
#include <QJsonDocument>
#include <QQmlComponent>

#include "AppController.h"
#include "ChatListModel.h"
#include "TackyBackend.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestChatList : public QObject {
    Q_OBJECT

private slots:
    // The badge is the row's only sign of unread mail, so it has to carry the
    // count and, once the chat is read, leave without a trace.
    void unreadChatsWearABadge() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"a@example.com","name":"Amy","last_activity":300,"unread":3},
            {"jid":"b@example.com","name":"Bob","last_activity":200,"unread":0},
            {"jid":"c@example.com","name":"Cy","last_activity":100,"unread":140}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *list = findItem(win.contentItem(), "chatList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 3);
        win.grabWindow(); // force the delegates to lay out and bind

        auto rowAt = [&](int i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
            return item;
        };
        auto badge = [&](int i) { return findItem(rowAt(i), "unreadBadge"); };
        auto badgeText = [&](int i) {
            QQuickItem *text = findItem(rowAt(i), "unreadCount");
            return text ? text->property("text").toString() : QString();
        };

        QVERIFY(badge(0));
        QVERIFY(badge(0)->isVisible());
        QCOMPARE(badgeText(0), QString("3"));
        // Nothing unread: the item exists but stays hidden, so the layout
        // skips it rather than leaving a gap at the row's end.
        QVERIFY(badge(1));
        QVERIFY(!badge(1)->isVisible());
        // Three digits would eat the name.
        QCOMPARE(badgeText(2), QString("99+"));

        // Opening the chat marks it read, which comes back as an <Item>.
        chats->applyItem(QVariantMap{{"jid", "a@example.com"},
                                     {"name", "Amy"},
                                     {"last_activity", 300},
                                     {"unread", 0}});
        QTRY_VERIFY(!badge(0)->isVisible());

        e.assertNoErrors();
    }

    // A room's row says what state it is in, which is the only place a failed
    // join or a room we have been dropped from is visible without opening it.
    void roomRowsAreStyledByTheirState() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","last_activity":600},
            {"jid":"ok@muc.example.com?join","name":"Joined","groupchat":true,
             "room_state":"joined","last_activity":500,
             "unread":4,"unread_mentions":1},
            {"jid":"bad@muc.example.com?join","name":"Failed","groupchat":true,
             "room_state":"error","room_reason":"forbidden","last_activity":400},
            {"jid":"gone@muc.example.com?join","name":"Dropped","groupchat":true,
             "room_state":"disconnected","last_activity":300},
            {"jid":"wait@muc.example.com?join","name":"Joining","groupchat":true,
             "room_state":"joining","last_activity":200},
            {"jid":"idle@muc.example.com?join","name":"Idle","groupchat":true,
             "room_state":"idle","last_activity":100}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 700);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *list = findItem(win.contentItem(), "chatList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 6);
        win.grabWindow(); // force the delegates to lay out and bind

        auto rowAt = [&](int i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
            return item;
        };
        auto title = [&](int i) { return findItem(rowAt(i), "chatRowTitle"); };
        auto colorOf = [&](int i) {
            return title(i)->property("color").value<QColor>();
        };
        const auto themeColor = [&](const char *name) {
            return theme->property(name).value<QColor>();
        };

        // A 1:1 chat has no room state to colour it by.
        QCOMPARE(colorOf(0), themeColor("textPrimary"));
        QCOMPARE(colorOf(1), themeColor("textPrimary")); // joined reads as normal
        QCOMPARE(colorOf(2), themeColor("negative"));    // the join failed
        QCOMPARE(colorOf(3), themeColor("warning"));     // member, but not in it
        QCOMPARE(colorOf(4), themeColor("textDim"));     // on its way in
        QCOMPARE(colorOf(5), themeColor("textDim"));     // never attempted

        // Only the transient state is italic, so it does not just read as one
        // more dimmed idle room.
        QVERIFY(title(4)->property("font").value<QFont>().italic());
        QVERIFY(!title(5)->property("font").value<QFont>().italic());

        auto mention = [&](int i) { return findItem(rowAt(i), "mentionMark"); };
        QVERIFY(mention(1)->isVisible());
        QVERIFY(!mention(0)->isVisible());
        // Unread without a mention is the badge alone.
        chats->applyItem(QVariantMap{{"jid", "ok@muc.example.com?join"},
                                     {"name", "Joined"},
                                     {"groupchat", true},
                                     {"room_state", "joined"},
                                     {"last_activity", 500},
                                     {"unread", 4},
                                     {"unread_mentions", 0}});
        QTRY_VERIFY(!mention(1)->isVisible());
        QVERIFY(findItem(rowAt(1), "unreadBadge")->isVisible());

        e.assertNoErrors();
    }

    // Without this the wide layout is a chat beside a list that never says
    // which row it came from.
    void theOpenChatIsMarkedInTheList() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","last_activity":300},
            {"jid":"bob@example.com","name":"Bob","last_activity":200},
            {"jid":"cy@example.com","name":"Cy","last_activity":100}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"currentJid", "bob@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *list = findItem(win.contentItem(), "chatList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 3);
        win.grabWindow(); // force the delegates to lay out and bind

        auto rowAt = [&](int i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
            return item;
        };
        auto tint = [&](int i) { return findItem(rowAt(i), "currentChatTint"); };
        auto tab = [&](int i) { return findItem(rowAt(i), "currentChatTab"); };

        QVERIFY(tint(1));
        // Over the whole row, not a sliver of it: an anchor that missed leaves
        // a tint that is on but invisible.
        QCOMPARE(itemRect(tint(1), rowAt(1)),
                 QRectF(0, 0, rowAt(1)->width(), rowAt(1)->height()));
        QVERIFY(rowAt(1)->height() > 0);
        QTRY_VERIFY(tint(1)->opacity() > 0.3);
        QVERIFY(tab(1)->isVisible());
        QVERIFY(tab(1)->height() > 0);

        // And on that row alone.
        QCOMPARE(tint(0)->opacity(), 0.0);
        QCOMPARE(tint(2)->opacity(), 0.0);
        QVERIFY(!tab(0)->isVisible());
        QVERIFY(!tab(2)->isVisible());

        // The mark follows the open chat rather than staying where it was put.
        page->setProperty("currentJid", "cy@example.com");
        QTRY_VERIFY(tint(2)->opacity() > 0.3);
        QTRY_COMPARE(tint(1)->opacity(), 0.0);
        QTRY_VERIFY(tab(2)->isVisible());
        QTRY_VERIFY(!tab(1)->isVisible());

        // Nothing open marks nothing.
        page->setProperty("currentJid", "");
        QTRY_COMPARE(tint(2)->opacity(), 0.0);

        e.assertNoErrors();
    }

    // Ctrl+Tab's step: down the list as it stands, wrapping at the foot, and
    // the row scrolled to - a mark off screen reads as the key doing nothing.
    void cyclingWalksTheListAndScrollsToTheRow() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        QVariantList entries;
        for (int i = 0; i < 12; ++i)
            entries << QVariantMap{{"jid", QString("c%1@example.com").arg(i)},
                                   {"name", QString("Chat %1").arg(i)},
                                   {"last_activity", 1200 - i}};
        chats->applyList(entries);

        QQuickWindow win;
        win.resize(360, 400); // room for fewer rows than there are chats
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *list = findItem(win.contentItem(), "chatList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 12);
        win.grabWindow(); // force the delegates to lay out and bind

        QSignalSpy opened(page, SIGNAL(openChat(QString, QString, bool)));
        // The shell's binding, by hand: it is what makes each step count from
        // where the last landed.
        auto cycle = [&](int delta) {
            QVERIFY(QMetaObject::invokeMethod(page, "cycleChat",
                                              Q_ARG(QVariant, QVariant(delta))));
            if (!opened.isEmpty())
                page->setProperty("currentJid", opened.last().at(0).toString());
        };

        // Nothing open: forward starts at the head of the list.
        cycle(1);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.last().at(0).toString(), QString("c0@example.com"));
        QCOMPARE(opened.last().at(1).toString(), QString("Chat 0"));
        QCOMPARE(opened.last().at(2).toBool(), false);

        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c1@example.com"));
        cycle(-1);
        QCOMPARE(opened.last().at(0).toString(), QString("c0@example.com"));
        // Back off the head is the foot, which a viewport this short was not
        // showing.
        cycle(-1);
        QCOMPARE(opened.last().at(0).toString(), QString("c11@example.com"));

        QQuickItem *last = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, last),
                                  Q_ARG(int, 11));
        QVERIFY2(last, "the row cycled to was never brought into the view");
        const QRectF seen = itemRect(last, list);
        QVERIFY2(seen.top() >= 0 && seen.bottom() <= list->height() + 0.5,
                 qPrintable(QString("row 11 sits at %1..%2 of a %3 viewport")
                                .arg(seen.top())
                                .arg(seen.bottom())
                                .arg(list->height())));

        // And forward off the foot is the head again.
        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c0@example.com"));

        // What the search box hides is not on the walk.
        QQuickItem *field = findItem(win.contentItem(), "searchField");
        QVERIFY(field);
        field->setProperty("text", "Chat 1");
        QTRY_COMPARE(list->property("count").toInt(), 3); // 1, 10, 11
        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c1@example.com"));
        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c10@example.com"));
        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c11@example.com"));
        cycle(1);
        QCOMPARE(opened.last().at(0).toString(), QString("c1@example.com"));

        // Nothing matching is nothing to step to, and no chat is opened.
        field->setProperty("text", "nothing matches this");
        QTRY_COMPARE(list->property("count").toInt(), 0);
        const int before = opened.count();
        cycle(1);
        QCOMPARE(opened.count(), before);

        e.assertNoErrors();
    }

    // A touch point carries no button, so the row's right-click handler was
    // offered every tap: on a phone the menu came up over the chat it opened.
    void aTouchTapOnARowOnlyOpensTheChat() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","source":"roster",
             "last_activity":300}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *list = findItem(win.contentItem(), "chatList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 1);
        win.grabWindow(); // force the delegates to lay out and bind

        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                                  Q_ARG(int, 0));
        QVERIFY(row);
        QObject *menu = page->findChild<QObject *>("chatRowMenu");
        QVERIFY(menu);
        QSignalSpy opened(page, SIGNAL(openChat(QString, QString, bool)));

        static QPointingDevice *finger = QTest::createTouchDevice();
        const QPoint centre =
            row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::touchEvent(&win, finger).press(0, centre);
        QTest::touchEvent(&win, finger).release(0, centre);

        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), QString("amy@example.com"));
        QTest::qWait(300); // longer than it takes a menu to open
        QVERIFY2(!menu->property("opened").toBool(),
                 "a tap on a row brought up its menu");

        e.assertNoErrors();
    }

    // The row menu is one instance shared by every row, so which verbs it shows
    // is entirely a function of the entry it was opened for. A contact offered
    // a room's join, or a room offered a call, would both be nonsense.
    void rowMenuFollowsTheRowItWasOpenedFor() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","source":"roster",
             "last_activity":300},
            {"jid":"room@muc.example.com?join","name":"The Room",
             "source":"bookmarks","groupchat":true,"autojoin":false,
             "room_state":"error","room_reason":"forbidden","last_activity":200},
            {"jid":"cy@example.com","name":"","source":"free",
             "last_activity":100}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        // A Popup is not in the page's visual tree until it opens, but it is
        // its QObject child from the start.
        QObject *menu = page->findChild<QObject *>("chatRowMenu");
        QVERIFY(menu);
        auto entry = [&](const char *name) {
            return menu->findChild<QObject *>(QLatin1String(name));
        };
        auto shown = [&](const char *name) {
            QObject *o = entry(name);
            return o && o->property("visible").toBool();
        };
        auto openFor = [&](const QString &jid) {
            QVERIFY(QMetaObject::invokeMethod(
                menu, "openFor", Q_ARG(QVariant, QVariant(chats->entryFor(jid)))));
        };

        openFor("amy@example.com");
        QVERIFY(shown("startCallEntry"));
        QVERIFY(!shown("joinEntry"));
        QVERIFY(!shown("forceJoinEntry"));
        QVERIFY(!shown("roomStatusLine"));
        // In the roster already, so there is nothing to add it to.
        QVERIFY(!shown("addContactEntry"));
        QVERIFY(shown("renameEntry"));
        QCOMPARE(entry("renameEntry")->property("text").toString(),
                 QString("Rename…"));
        QCOMPARE(menu->property("chatTitle").toString(), QString("Amy"));

        openFor("room@muc.example.com?join");
        QVERIFY(!shown("startCallEntry"));
        QVERIFY(shown("joinEntry"));
        QVERIFY(shown("forceJoinEntry"));
        // A room we are not a member of: the tick is the membership, and the
        // reason a join failed is the only actionable thing about the state.
        QCOMPARE(entry("joinEntry")->property("trailing").toString(), QString());
        QVERIFY(shown("roomStatusLine"));
        QCOMPARE(entry("roomStatusLine")->property("text").toString(),
                 QString("Join failed: forbidden"));
        QCOMPARE(entry("removeEntry")->property("text").toString(),
                 QString("Remove bookmark…"));

        // Chat history but no roster entry: the way in, rather than a rename
        // and a remove that would act on an item that isn't there.
        openFor("cy@example.com");
        QVERIFY(shown("addContactEntry"));
        QVERIFY(!shown("renameEntry"));
        QVERIFY(!shown("removeEntry"));
        QCOMPARE(menu->property("chatTitle").toString(),
                 QString("cy@example.com")); // unnamed: goes by its JID

        e.assertNoErrors();
    }

    // Starting a chat with someone not on the list yet: the chat opens either
    // way, and the roster write is what the tick decides. The JID is cut back to
    // bare lower case first, or the chat opened would be one the list can never
    // produce a row for.
    void newChatOpensTheChatAndOptionallyAddsTheContact() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QObject *sheet = page->findChild<QObject *>("newChatSheet");
        QVERIFY(sheet);
        QSignalSpy opened(page, SIGNAL(openChat(QString, QString, bool)));
        QSignalSpy sent(app->backend(), &TackyBackend::sent);

        QObject *jidField = sheet->findChild<QObject *>("newChatJid");
        QObject *nameField = sheet->findChild<QObject *>("newChatName");
        QVERIFY(jidField);
        QVERIFY(nameField);
        jidField->setProperty("text", "Amy@Example.com/Phone");
        nameField->setProperty("text", "Amy");
        QVERIFY(QMetaObject::invokeMethod(sheet, "accept"));

        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), QString("amy@example.com"));
        QCOMPARE(opened.at(0).at(1).toString(), QString("Amy"));
        QVERIFY(!opened.at(0).at(2).toBool()); // never a room
        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QString("roster"));
        QCOMPARE(sent.at(0).at(1).toString(), QString("add"));
        QCOMPARE(sent.at(0).at(2).toMap().value("jid").toString(),
                 QString("amy@example.com"));

        // Unticked, it is a chat and nothing more - a one-off reply should not
        // put a stranger in the roster.
        sent.clear();
        opened.clear();
        QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
        jidField->setProperty("text", "cy@example.com");
        sheet->findChild<QObject *>("newChatAddContact")->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(sheet, "accept"));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.at(0).at(0).toString(), QString("cy@example.com"));
        QCOMPARE(sent.count(), 0);

        e.assertNoErrors();
    }

    // Joining from the menu has to reach the backend as a bookmark write, and a
    // remove has to wait for the confirmation rather than fire on the click.
    void rowMenuEditsReachTheBackend() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"amy@example.com","name":"Amy","source":"roster",
             "last_activity":300},
            {"jid":"room@muc.example.com?join","name":"The Room",
             "source":"bookmarks","groupchat":true,"autojoin":false,
             "last_activity":200}
        ])")
                              .array()
                              .toVariantList());

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ConversationsPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QObject *menu = page->findChild<QObject *>("chatRowMenu");
        QVERIFY(menu);
        QSignalSpy sent(app->backend(), &TackyBackend::sent);
        auto lastFrame = [&] {
            if (sent.isEmpty())
                return QString();
            const QList<QVariant> &c = sent.last();
            return c.at(0).toString() + "/" + c.at(1).toString() + " "
                   + c.at(2).toMap().value("jid").toString();
        };

        QVERIFY(QMetaObject::invokeMethod(
            menu, "openFor",
            Q_ARG(QVariant, QVariant(chats->entryFor("room@muc.example.com?join")))));
        QVERIFY(QMetaObject::invokeMethod(menu->findChild<QObject *>("joinEntry"),
                                          "triggered"));
        QCOMPARE(lastFrame(), QString("bookmarks/item room@muc.example.com?join"));

        // Removing a contact is destructive, so the menu only opens the
        // question; nothing goes out until it is answered.
        const int before = sent.count();
        QVERIFY(QMetaObject::invokeMethod(
            menu, "openFor",
            Q_ARG(QVariant, QVariant(chats->entryFor("amy@example.com")))));
        QVERIFY(QMetaObject::invokeMethod(menu->findChild<QObject *>("removeEntry"),
                                          "triggered"));
        QCOMPARE(sent.count(), before);

        QObject *confirm = page->findChild<QObject *>("removeContactConfirm");
        QVERIFY(confirm);
        QCOMPARE(confirm->property("subject").toString(),
                 QString("amy@example.com"));
        QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
        QCOMPARE(lastFrame(), QString("roster/remove amy@example.com"));

        // Refresh asks the server again rather than re-reading what tacky has.
        sent.clear();
        QVERIFY(QMetaObject::invokeMethod(page->findChild<QObject *>("refreshEntry"),
                                          "triggered"));
        QStringList refreshed;
        for (const QList<QVariant> &c : sent)
            refreshed << c.at(0).toString() + "/" + c.at(1).toString();
        QCOMPARE(refreshed, QStringList({"roster/request", "bookmarks/request",
                                         "chatlist/get"}));

        e.assertNoErrors();
    }
};

QTEST_MAIN(TestChatList)
#include "tst_qmlload_chatlist.moc"
