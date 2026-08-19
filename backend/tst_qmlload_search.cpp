// The one search box over the conversations pane, and where a hit lands when it
// is walked into the chat it came from.
#include <QtTest>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QQmlComponent>

#include "AppController.h"
#include "ChatListFilter.h"
#include "ChatListModel.h"
#include "SearchModel.h"
#include "TackyBackend.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestSearch : public QObject {
    Q_OBJECT

private slots:
    // Hits are drawn from several chats at once, each of which brings its own
    // name cache - the part of the section that only runs with rows in it.
    void drawsSearchResults() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

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

        // The section is the list's footer, which the view builds on its first
        // layout rather than with the page.
        SearchModel *model = nullptr;
        QTRY_VERIFY((model = page->findChild<SearchModel *>()));
        model->setQuery("pizza");
        model->search(); // the backend is unstarted, so nothing answers it
        model->applyResult(
            QJsonDocument::fromJson(R"({"messages":[
                {"timestamp":400,"chat_jid":"a@example.com","from_jid":"a@example.com",
                 "is_outgoing":false,"content":{"type":"text","body":"pizza tonight?"}},
                {"timestamp":300,"chat_jid":"room@muc?join","from_jid":"room@muc/bo",
                 "is_outgoing":false,"content":{"type":"text","body":"cold pizza"}}],
                "complete":true,"last":"300 a@example.com"})")
                .object()
                .toVariantMap(),
            false);

        QCOMPARE(model->rowCount(), 2);
        QQuickItem *hits = findItem(win.contentItem(), "messageHits");
        QVERIFY(hits);
        // One name cache per chat the results touch, built as they arrive.
        QTRY_COMPARE(hits->property("authorsByChat").toMap().size(), 2);
        win.grabWindow(); // force the delegates to lay out and bind
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }

    // A heading stands only while there is something under it, so it changes
    // height as the list it is in re-lays out. Nothing it reads may come back
    // from that layout.
    void sectionHeadingsDoNotFightTheList() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"a@example.com","name":"Amy","last_activity":300},
            {"jid":"b@example.com","name":"Bob","last_activity":200}
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
        QTRY_COMPARE(list->property("count").toInt(), 2);
        win.grabWindow();

        QQuickItem *heading = list->property("headerItem").value<QQuickItem *>();
        QVERIFY(heading);
        QCOMPARE(heading->height(), 0.0); // nothing typed, nothing to head

        QQuickItem *field = findItem(win.contentItem(), "searchField");
        QVERIFY(field);
        field->setProperty("text", "Amy");
        QTRY_COMPARE(list->property("count").toInt(), 1);
        QTRY_VERIFY(heading->height() > 0);
        win.grabWindow();
        QCoreApplication::processEvents();

        // And the hits arriving grow the section under them.
        SearchModel *model = nullptr;
        QTRY_VERIFY((model = page->findChild<SearchModel *>()));
        model->applyResult(
            QJsonDocument::fromJson(R"({"messages":[
                {"timestamp":400,"chat_jid":"a@example.com","from_jid":"a@example.com",
                 "is_outgoing":false,"content":{"type":"text","body":"amsterdam"}}],
                "complete":true,"last":"400 a@example.com"})")
                .object()
                .toVariantMap(),
            false);
        QCOMPARE(model->rowCount(), 1);
        win.grabWindow();
        QCoreApplication::processEvents();

        // Typing on leaves no chat matching, so that heading goes again.
        field->setProperty("text", "Amyx");
        QTRY_COMPARE(list->property("count").toInt(), 0);
        QTRY_COMPARE(heading->height(), 0.0);
        win.grabWindow();
        QCoreApplication::processEvents();

        e.assertNoErrors();
    }

    // The roster arrives whole, so a search standing when one lands is standing
    // on a model that resets under it.
    void searchResultsSurviveAChatListReload() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        ChatListModel *chats = app->chatListFor("me@example.com");
        QVERIFY(chats);
        chats->applyList(QJsonDocument::fromJson(
                             R"([{"jid":"a@example.com","name":"Amy","last_activity":300}])")
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
        win.grabWindow();

        QQuickItem *field = findItem(win.contentItem(), "searchField");
        QVERIFY(field);
        field->setProperty("text", "Amy");

        SearchModel *model = nullptr;
        QTRY_VERIFY((model = page->findChild<SearchModel *>()));
        model->applyResult(
            QJsonDocument::fromJson(R"({"messages":[
                {"timestamp":400,"chat_jid":"a@example.com","from_jid":"a@example.com",
                 "is_outgoing":false,"content":{"type":"text","body":"amy said so"}}],
                "complete":true,"last":"400 a@example.com"})")
                .object()
                .toVariantMap(),
            false);
        QCOMPARE(model->rowCount(), 1);

        // The reload a fresh roster is: every row replaced at once.
        chats->applyList(QJsonDocument::fromJson(R"([
            {"jid":"a@example.com","name":"Amy","last_activity":300},
            {"jid":"b@example.com","name":"Bob","last_activity":200}
        ])")
                             .array()
                             .toVariantList());
        win.grabWindow();
        QCoreApplication::processEvents();

        // Rebuilding the section would have dropped the results and gone back
        // to the archive for them.
        QCOMPARE(page->findChild<SearchModel *>(), model);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(findItem(win.contentItem(), "messageHits"));
    }

    // One box, both halves of what a typed word can mean: the chats it names,
    // narrowed to at once, and the messages behind them once the typing
    // settles.
    void oneBoxSearchesChatsAndMessages() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

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

        QQuickItem *field = findItem(win.contentItem(), "searchField");
        QVERIFY(field);
        field->setProperty("text", "pizza");

        // The list narrows on the keystroke.
        auto *filter = page->findChild<ChatListFilter *>();
        QVERIFY(filter);
        QCOMPARE(filter->query(), QStringLiteral("pizza"));

        // The archive is a round trip, so it comes after the pause. Watch the
        // frame rather than `searching`: the backend is unstarted, so the
        // request is answered with an error the moment it goes out.
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QSignalSpy sent(app->backend(), &TackyBackend::sent);
        SearchModel *model = nullptr;
        QTRY_VERIFY((model = page->findChild<SearchModel *>()));
        QCOMPARE(model->query(), QStringLiteral("pizza"));
        QTRY_VERIFY(asked(sent, "message", "search"));

        // And emptying the box calls it off rather than searching for nothing.
        sent.clear();
        field->setProperty("text", "");
        QCOMPARE(filter->query(), QString());
        QVERIFY(!asked(sent, "message", "search"));
    }

    // The mark on the empty pane is the app icon out of the Qt resource system,
    // which is wired separately from the module's own QML files - it would
    // resolve in the app and not here if the resource sat on the executable
    // instead of on `quack`. Nothing but a loaded image tells the two apart, so
    // this reads the status rather than the source it was given.
    void theEmptyPaneDrawsTheAppIcon() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

        QQuickWindow win;
        win.resize(420, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        // No chatJid, which is what leaves the pane showing its empty half.
        QQmlComponent comp(&e, "Quack", "ChatPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        QVERIFY(!page->property("hasChat").toBool());
        page->setParentItem(win.contentItem());

        QQuickItem *mark = findItem(win.contentItem(), "emptyMark");
        QVERIFY(mark);
        QVERIFY(mark->isVisible());

        const QMetaObject *mo = mark->metaObject();
        const QMetaEnum status = mo->enumerator(mo->indexOfEnumerator("Status"));
        QTRY_COMPARE(mark->property("status").toInt(), status.keyToValue("Ready"));
        // The implicit size is the decoded image's, so this is the pixels
        // arriving rather than the box they were asked for.
        QVERIFY(mark->property("implicitWidth").toReal() > 0);
        QVERIFY(mark->property("implicitHeight").toReal() > 0);

        e.assertNoErrors();
    }

    // Searching inside a chat walks the hits in the feed instead of listing
    // them, so the step - and what the counter says about it - is the feature.
    void walksHitsInsideTheChat() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

        QQuickWindow win;
        win.resize(420, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ChatPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"chatJid", "friend@example.com"},
             {"chatName", "Friend"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QVERIFY(QMetaObject::invokeMethod(page, "openSearch"));
        QVERIFY(page->property("searchMode").toBool());
        auto *model = page->findChild<SearchModel *>();
        QVERIFY(model);

        QQuickItem *field = findItem(win.contentItem(), "chatSearchField");
        QVERIFY(field);
        // Typing is the whole of it - nothing here presses Enter.
        field->setProperty("text", "pizza");
        auto *engineApp = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(engineApp);
        QSignalSpy searchSent(engineApp->backend(), &TackyBackend::sent);
        QTRY_VERIFY_WITH_TIMEOUT(asked(searchSent, "message", "search"), 3000);
        model->applyResult(QJsonDocument::fromJson(R"({"messages":[
            {"timestamp":300,"chat_jid":"friend@example.com"},
            {"timestamp":200,"chat_jid":"friend@example.com"},
            {"timestamp":100,"chat_jid":"friend@example.com"}],
            "complete":true,"last":"100"})")
                               .object()
                               .toVariantMap(),
                           false);

        // The newest hit is where it lands, and the counter counts from one.
        QTRY_COMPARE(page->property("hitIndex").toInt(), 0);
        QQuickItem *counter = findItem(win.contentItem(), "hitCounter");
        QVERIFY(counter);
        QCOMPARE(counter->property("text").toString(), QString("1/3"));

        // Return walks towards older messages, which is down the newest-first
        // list; shifted, it walks back up. Moving the counter is half of it -
        // the step is only real if the feed is told to go there.
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QSignalSpy sent(app->backend(), &TackyBackend::sent);
        QTest::keyClick(&win, Qt::Key_Return);
        QCOMPARE(page->property("hitIndex").toInt(), 1);
        QCOMPARE(counter->property("text").toString(), QString("2/3"));
        QVariantMap jump;
        for (const QList<QVariant> &call : sent)
            if (call.at(0).toString() == "message" && call.at(1).toString() == "goto")
                jump = call.at(2).toMap();
        QCOMPARE(jump.value("date").toLongLong(), 200);
        // Local, so the step lands now rather than after a MAM round trip that
        // the next keypress would cancel anyway.
        QCOMPARE(jump.value("source").toString(), QString("local"));
        QTest::keyClick(&win, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(page->property("hitIndex").toInt(), 0);
        // Nothing newer than the newest.
        QTest::keyClick(&win, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(page->property("hitIndex").toInt(), 0);

        QVariant closed;
        QVERIFY(QMetaObject::invokeMethod(page, "closeSearch",
                                          Q_RETURN_ARG(QVariant, closed)));
        QVERIFY(closed.toBool());
        QVERIFY(!page->property("searchMode").toBool());
        QCOMPARE(model->count(), 0);

        win.grabWindow();
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }
};

QTEST_MAIN(TestSearch)
#include "tst_qmlload_search.moc"
