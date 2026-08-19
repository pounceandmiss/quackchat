// Loads the app QML headless with the real registered types. Binding errors
// (ReferenceError etc.) only surface as warnings, so collect and fail on them.
#include <QtTest>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <QGuiApplication>

#include "AccountSettings.h"
#include "AppController.h"
#include "AvatarEncoder.h"
#include "CallsModel.h"
#include "ChatListFilter.h"
#include "ChatListModel.h"
#include "MucRoomModel.h"
#include "OmemoDevicesModel.h"
#include "RegistrationController.h"
#include "SearchModel.h"
#include "TackyBackend.h"

namespace {
// Stands in for the GUI's QImage encoder, so a publish can be put in flight
// without one. The engine's backend is never started here, so the request goes
// no further than its token.
class StubEncoder : public AvatarEncoder {
public:
    AvatarImage encode(const QUrl &, QString *) const override {
        return {QByteArray("stub-avatar-bytes"), 128, 128};
    }
};

// An engine that keeps the warnings it emits. A binding error fails nothing by
// itself, so a test that never reads them back goes green over a page that drew
// nothing. Every test drives one of these and ends by asking what it saw.
template <class Base>
class CollectingEngine : public Base {
public:
    CollectingEngine() {
        QObject::connect(this, &QQmlEngine::warnings,
                         [this](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 m_warnings << w.toString();
                         });
    }

    void assertNoErrors() const {
        for (const QString &w : m_warnings)
            if (w.contains("ReferenceError") || w.contains("is not defined") ||
                w.contains("TypeError") ||
                // A layout whose size depends on what it is sizing. The layout
                // gives up after two passes and leaves whatever it had, so this
                // is a real defect that otherwise only shows as a stray line on
                // stderr.
                w.contains("recursive rearrange") ||
                // A property that feeds itself. Qt breaks the cycle wherever
                // it notices, so what is on screen is whichever pass got there
                // last.
                w.contains("Binding loop"))
                QFAIL(qPrintable("QML error: " + w));
    }

private:
    QStringList m_warnings;
};

using Engine = CollectingEngine<QQmlEngine>;
using AppEngine = CollectingEngine<QQmlApplicationEngine>;
} // namespace

class TestQmlLoad : public QObject {
    Q_OBJECT

    static QVariant device(int id, const QString &trust) {
        return QVariantMap{{"device", id},
                           {"trust", trust},
                           {"active", true},
                           {"fingerprint", QString(64, QChar('a'))}};
    }

    // Repeater delegates hang off the visual tree, not the QObject one, so
    // findChild never sees them.
    // Did a frame for this method go out, whatever else did.
    static bool asked(const QSignalSpy &spy, const QString &module,
                      const QString &method) {
        for (const QList<QVariant> &call : spy)
            if (call.at(0).toString() == module && call.at(1).toString() == method)
                return true;
        return false;
    }

    static QQuickItem *findItem(QQuickItem *root, const QString &name) {
        if (!root)
            return nullptr;
        const auto children = root->childItems();
        for (QQuickItem *child : children) {
            if (child->objectName() == name)
                return child;
            if (QQuickItem *found = findItem(child, name))
                return found;
        }
        return nullptr;
    }

    // Every item under `root` named `name`, for the repeated parts of a list.
    static QList<QQuickItem *> findItems(QQuickItem *root, const QString &name) {
        QList<QQuickItem *> out;
        if (!root)
            return out;
        const auto children = root->childItems();
        for (QQuickItem *child : children) {
            if (child->objectName() == name)
                out << child;
            out << findItems(child, name);
        }
        return out;
    }

    // Call windows are built for every row but shown one at a time, so what is
    // on screen is the only question worth asking. topLevelWindows() counts the
    // hidden ones too.
    static QList<QWindow *> visibleWindows() {
        QList<QWindow *> out;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w->isVisible())
                out << w;
        return out;
    }

    static QWindow *visibleWindowTitled(const QString &fragment) {
        for (QWindow *w : visibleWindows())
            if (w->title().contains(fragment))
                return w;
        return nullptr;
    }

    // Where an item lands in some ancestor's coordinates, which is the only
    // way to compare two that do not share a parent.
    static QRectF itemRect(QQuickItem *item, QQuickItem *within) {
        return item->mapRectToItem(within,
                                   QRectF(0, 0, item->width(), item->height()));
    }

    // A window with a ChatBubble component ready to build, and a real 24x12
    // thumbnail on disk. The '#' in the directory is why the model hands over a
    // url and not a path - "file:" concatenated onto this one loses everything
    // after it.
    class Bubbles {
    public:
        explicit Bubbles(QQmlEngine &e) : m_comp(&e, "Quack", "ChatBubble") {
            if (!m_dir.isValid() || !QDir(m_dir.path()).mkdir("od#d")) {
                m_error = QStringLiteral("no temporary directory to work in");
                return;
            }
            m_thumb = m_dir.filePath("od#d/a_320.png");
            QImage png(24, 12, QImage::Format_RGB32);
            png.fill(Qt::red);
            if (!png.save(m_thumb)) {
                m_error = "could not write " + m_thumb;
                return;
            }
            m_win.resize(500, 400);
            m_win.show();
            if (!QTest::qWaitForWindowExposed(&m_win)) {
                m_error = QStringLiteral("the window never appeared");
                return;
            }
            if (!m_comp.isReady())
                m_error = m_comp.errorString();
        }

        QString error() const { return m_error; }
        QString thumbPath() const { return m_thumb; }

        // Width up front: the bubble sizes itself off its parent, and one built
        // parentless would fall back to measuring its own content instead.
        QQuickItem *with(const QVariantMap &att) {
            auto *item = qobject_cast<QQuickItem *>(m_comp.createWithInitialProperties(
                {{"attachments", QVariantList{att}},
                 {"text", ""},
                 {"width", m_win.width()}}));
            if (item)
                item->setParentItem(m_win.contentItem());
            return item;
        }

        void tap(QQuickItem *target) {
            const QPoint p = m_win.contentItem()
                                 ->mapFromItem(target, QPointF(target->width() / 2,
                                                               target->height() / 2))
                                 .toPoint();
            QTest::mouseClick(&m_win, Qt::LeftButton, Qt::NoModifier, p);
        }

    private:
        QTemporaryDir m_dir;
        QQuickWindow m_win;
        QQmlComponent m_comp;
        QString m_thumb;
        QString m_error;
    };

    // As ChatModel hands them over: what the message said, merged with the
    // state of the transfer.
    static QVariantMap attachment(const QString &type, const QString &name,
                                  const QString &thumb, const QString &state) {
        return QVariantMap{
            {"url", "https://h/" + name},
            {"type", type},
            {"name", name},
            {"size", 2048},
            {"mime", ""},
            {"state", state},
            // What the hint and the retry are worded from.
            {"direction", state.isEmpty() ? QString() : QStringLiteral("download")},
            {"loaded", 0},
            {"total", 0},
            {"localpath", ""},
            {"thumburl", thumb.isEmpty() ? QUrl() : QUrl::fromLocalFile(thumb)},
            {"error", ""}};
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

    // One contact's details window, held open by `holder`.
    static QQuickWindow *openContactDetails(QQmlEngine &e,
                                            QScopedPointer<QObject> &holder) {
        QQmlComponent comp(&e, "Quack", "ContactDetailsWindow");
        if (!comp.isReady()) {
            qWarning("%s", qPrintable(comp.errorString()));
            return nullptr;
        }
        holder.reset(comp.createWithInitialProperties({{"account", "me@example.com"},
                                                       {"jid", "friend@example.com"},
                                                       {"name", "Friend"}}));
        auto *w = qobject_cast<QQuickWindow *>(holder.data());
        if (!w)
            return nullptr;
        w->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();
        return w;
    }

    // The roster entry the contact page reads itself through.
    static void rosterEntry(AppController *app, const QString &subscription,
                            const QString &ask) {
        ChatListModel *chats = app->chatListFor("me@example.com");
        if (!chats)
            return;
        QVariantMap entry{{"jid", "friend@example.com"},
                          {"name", "Friend Renamed"},
                          {"source", "roster"},
                          {"subscription", subscription},
                          {"last_activity", 300}};
        if (!ask.isEmpty())
            entry["ask"] = ask;
        chats->applyList({entry});
        QCoreApplication::processEvents();
    }

    // One room's details window, held open by `holder`.
    static QQuickWindow *openMucDetails(QQmlEngine &e,
                                        QScopedPointer<QObject> &holder) {
        QQmlComponent comp(&e, "Quack", "MucDetailsWindow");
        if (!comp.isReady()) {
            qWarning("%s", qPrintable(comp.errorString()));
            return nullptr;
        }
        holder.reset(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"jid", "room@muc.example.com?join"},
             {"name", "The Room"}}));
        auto *w = qobject_cast<QQuickWindow *>(holder.data());
        if (!w)
            return nullptr;
        w->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();
        return w;
    }

    // One occupant of each role, us among them as "amy". Only the moderator has
    // an address the room discloses, and only the visitor has nothing at all.
    static MucRoomModel *joinedRoom(QQuickWindow *w) {
        auto *room = w->findChild<MucRoomModel *>();
        if (!room)
            return nullptr;
        room->applyJoined(true);
        room->applyOccupants({QVariantMap{{"nick", "mo"},
                                          {"role", "moderator"},
                                          {"affiliation", "owner"},
                                          {"jid", "mo@elsewhere.example"},
                                          {"caps", QVariantMap{{"kick", true}}}},
                              QVariantMap{{"nick", "amy"}, {"role", "participant"},
                                          {"affiliation", "none"},
                                          {"status", "online"}},
                              QVariantMap{{"nick", "zoe"}, {"role", "visitor"},
                                          {"affiliation", "member"}}});
        room->applyMyNick("amy");
        room->applySubject("what this room is about");
        QCoreApplication::processEvents();
        return room;
    }

    // The window AppWindows hands back for a second identical request is the one
    // it already made: a second view of the same state would argue with the
    // first over what is on screen. Asked from inside a test that holds a page
    // of its own, since nothing here calls closeAll and AppWindows' window would
    // otherwise be the last thing alive at teardown.
    static void oneWindowNotTwo(const QVariant &first, const QVariant &again) {
        QVERIFY(first.value<QObject *>());
        QCOMPARE(again.value<QObject *>(), first.value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(first.value<QObject *>(), "close"));
        QCoreApplication::processEvents();
    }

private slots:
    // The app's own startup, then quit with a settings window open - which
    // segfaulted until AppWindows took its own windows down first. The app does
    // that from Qt.application.aboutToQuit, which no test can raise without
    // ending the whole run, so this drives closeAll() directly.
    void closingTheWindowsItHoldsSurvivesShutdown() {
        AppEngine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());

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

    void loadsApp() {
        AppEngine e;
        // create the App singleton up front; backend stays unstarted
        e.singletonInstance<AppController *>("Quack", "App");
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());
        auto *win = qobject_cast<QQuickWindow *>(e.rootObjects().first());
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

    // The bubble decides, from the attachment alone, whether a tap opens the
    // file or has to fetch it first - the same rule the Tk client draws on.
    void aDownloadedImageDrawsItsThumbnailAndOpensOnATap() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("image", "a.png", bubbles.thumbPath(), "done")));
        QVERIFY(!b.isNull());
        QQuickItem *thumb = findItem(b.data(), "attachmentThumb");
        QQuickItem *chip = findItem(b.data(), "attachmentChip");
        QVERIFY(thumb);
        QVERIFY(chip);
        QVERIFY(thumb->isVisible());
        QVERIFY(!chip->isVisible());
        QTRY_VERIFY(thumb->width() > 0);
        QCOMPARE(thumb->height() / thumb->width(), 0.5); // 24x12, kept

        QSignalSpy opened(b.data(), SIGNAL(attachmentOpenRequested(int)));
        QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
        bubbles.tap(thumb);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.first().at(0).toInt(), 0);
        QCOMPARE(loaded.count(), 0);

        e.assertNoErrors();
    }

    // One nobody has fetched yet is a chip, and a tap fetches it.
    void anUnfetchedImageIsAChipThatFetches() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("image", "b.png", "", "")));
        QVERIFY(!b.isNull());
        QQuickItem *chip = findItem(b.data(), "attachmentChip");
        QVERIFY(chip);
        QVERIFY(chip->isVisible());
        QVERIFY(!findItem(b.data(), "attachmentThumb")->isVisible());

        QSignalSpy opened(b.data(), SIGNAL(attachmentOpenRequested(int)));
        QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
        bubbles.tap(chip);
        QCOMPARE(loaded.count(), 1);
        QCOMPARE(opened.count(), 0);

        e.assertNoErrors();
    }

    // A plain file has no thumbnail to wait for: its tap opens, which downloads
    // first if it has to.
    void aPlainFileOpensOnATapAndSaysItsSize() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("file", "doc.pdf", "", "")));
        QVERIFY(!b.isNull());
        QQuickItem *chip = findItem(b.data(), "attachmentChip");
        QVERIFY(chip);
        QVERIFY(chip->isVisible());

        QSignalSpy opened(b.data(), SIGNAL(attachmentOpenRequested(int)));
        bubbles.tap(chip);
        QCOMPARE(opened.count(), 1);

        QVariant size;
        QVERIFY(QMetaObject::invokeMethod(b.data(), "fmtSize",
                                          Q_RETURN_ARG(QVariant, size),
                                          Q_ARG(QVariant, 2048)));
        QCOMPARE(size.toString(), QString("2.0 KB"));

        e.assertNoErrors();
    }

    // An image tacky held back, capped or cancelled ends `idle`: nothing on
    // disk and no error to report, so it draws the same tap-to-load chip as one
    // nobody has asked for.
    void aHeldBackImageDrawsTheTapToLoadChip() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("image", "d.png", "", "idle")));
        QVERIFY(!b.isNull());
        QQuickItem *chip = findItem(b.data(), "attachmentChip");
        QVERIFY(chip);
        QVERIFY(chip->isVisible());
        QVERIFY(!findItem(b.data(), "attachmentProgress")->isVisible());

        QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
        bubbles.tap(chip);
        QCOMPARE(loaded.count(), 1);

        e.assertNoErrors();
    }

    void aFailedTransferRetriesInsteadOfOpeningNothing() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("file", "doc.pdf", "", "failed")));
        QVERIFY(!b.isNull());
        QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
        bubbles.tap(findItem(b.data(), "attachmentChip"));
        QCOMPARE(loaded.count(), 1);

        e.assertNoErrors();
    }

    // Our own share on its way out: the same bar as a download, worded for the
    // direction, and the picture stays up while its bytes go.
    void anOutgoingShareKeepsItsPictureUpWhileItSends() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QVariantMap up = attachment("image", "e.png", bubbles.thumbPath(), "active");
        up["direction"] = "upload";
        up["loaded"] = 50;
        up["total"] = 100;
        QScopedPointer<QQuickItem> b(bubbles.with(up));
        QVERIFY(!b.isNull());
        QVERIFY(findItem(b.data(), "attachmentThumb")->isVisible());
        QVERIFY(findItem(b.data(), "attachmentProgress")->isVisible());
        QCOMPARE(findItem(b.data(), "attachmentHint")->property("text").toString(),
                 QString("Uploading…"));

        e.assertNoErrors();
    }

    // With no message from tacky to show, it still has to say which half of the
    // trip failed.
    void aFailedUploadSaysWhichHalfOfTheTripFailed() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QVariantMap up = attachment("file", "doc.pdf", "", "failed");
        up["direction"] = "upload";
        QScopedPointer<QQuickItem> b(bubbles.with(up));
        QVERIFY(!b.isNull());
        QCOMPARE(findItem(b.data(), "attachmentHint")->property("text").toString(),
                 QString("Upload failed"));

        e.assertNoErrors();
    }

    // Everything but Cancel acts on a finished file, so a transfer still
    // running offers only the way to stop it, and the other way round once it
    // is done.
    void theAttachmentMenuFollowsTheTransfer() {
        Engine e;
        Bubbles bubbles(e);
        QVERIFY2(bubbles.error().isEmpty(), qPrintable(bubbles.error()));

        QScopedPointer<QQuickItem> b(
            bubbles.with(attachment("file", "doc.pdf", "", "done")));
        QVERIFY(!b.isNull());
        QObject *menu = b->findChild<QObject *>("attachmentMenu");
        QVERIFY(menu);
        auto offered = [&](const char *name) {
            QObject *o = menu->findChild<QObject *>(QLatin1String(name));
            return o && o->property("offered").toBool();
        };

        QVariantMap busy = attachment("file", "doc.pdf", "", "active");
        QVERIFY(QMetaObject::invokeMethod(menu, "openFor", Q_ARG(QVariant, 0),
                                          Q_ARG(QVariant, QVariant(busy))));
        QVERIFY(offered("attachmentCancelEntry"));
        QVERIFY(!offered("attachmentSaveEntry"));
        QVERIFY(!offered("attachmentUncacheEntry"));

        QVERIFY(QMetaObject::invokeMethod(
            menu, "openFor", Q_ARG(QVariant, 0),
            Q_ARG(QVariant, QVariant(attachment("file", "doc.pdf", "", "done")))));
        QVERIFY(!offered("attachmentCancelEntry"));
        QVERIFY(offered("attachmentSaveEntry"));
        QVERIFY(offered("attachmentFolderEntry"));
        QVERIFY(offered("attachmentUncacheEntry"));

        // And each one asks the page for the attachment it was opened on.
        QSignalSpy save(b.data(), SIGNAL(attachmentSaveRequested(int)));
        QVERIFY(QMetaObject::invokeMethod(
            menu->findChild<QObject *>("attachmentSaveEntry"), "triggered"));
        QCOMPARE(save.count(), 1);
        QCOMPARE(save.first().at(0).toInt(), 0);

        e.assertNoErrors();
    }

    // A failed row picks which half to run again from what tacky said about
    // the transfer: one that never got its file up has no message to resend.
    void chatPageSharesFilesAndRetriesTheRightHalf() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));

        QQuickWindow win;
        win.resize(500, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ChatPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"chatJid", "amy@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        QQuickItem *attach = findItem(page, "attachButton");
        QVERIFY(attach);
        QVERIFY(attach->isVisible());

        auto isFailedUpload = [&](const QVariant &att) {
            QVariant out;
            [&] {
                QVERIFY(QMetaObject::invokeMethod(page, "isFailedUpload",
                                                  Q_RETURN_ARG(QVariant, out),
                                                  Q_ARG(QVariant, att)));
            }();
            return out.toBool();
        };
        auto transfer = [](const QString &direction, const QString &state) {
            return QVariant(QVariantMap{{"direction", direction}, {"state", state}});
        };

        QVERIFY(isFailedUpload(transfer("upload", "failed")));
        // A fetch that failed is the download's to run again, not the send's.
        QVERIFY(!isFailedUpload(transfer("download", "failed")));
        // And one still going out is not a retry at all.
        QVERIFY(!isFailedUpload(transfer("upload", "active")));
        // Every failed text message asks this on its way to `resend`.
        QVERIFY(!isFailedUpload(QVariant()));

        e.assertNoErrors();
    }

    // The image settings left the overflow menu for a page of their own. The
    // dots say what is in force, and picking one writes it through.
    void imageSettingsArePickedFromThePreferencesPage() {
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

        // A menu's rows are its items, and there are none until it is shown.
        QObject *menu = page->findChild<QObject *>("overflowMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));

        auto entry = [&](const char *name) -> QObject * {
            const int count = menu->property("count").toInt();
            for (int i = 0; i < count; ++i) {
                QQuickItem *item = nullptr;
                if (!QMetaObject::invokeMethod(menu, "itemAt",
                                               Q_RETURN_ARG(QQuickItem *, item),
                                               Q_ARG(int, i)))
                    return nullptr;
                if (item && item->objectName() == QLatin1String(name))
                    return item;
            }
            return nullptr;
        };

        // One entry where seven settings were.
        QVERIFY(entry("preferencesEntry"));
        QVERIFY2(!entry("autofetch_everyone"),
                 "the image settings are still flattened into the menu");
        QVERIFY2(!entry("autofetchMax_0"),
                 "the size cap is still flattened into the menu");

        // And this is where they went.
        QQmlComponent prefsComp(&e, "Quack", "AppSettingsWindow");
        QVERIFY2(prefsComp.isReady(), qPrintable(prefsComp.errorString()));
        QScopedPointer<QObject> prefs(prefsComp.create());
        QVERIFY(!prefs.isNull());
        auto *prefsWin = qobject_cast<QQuickWindow *>(prefs.data());
        QVERIFY(prefsWin);
        // Taller than the shipped window so both cards are in the viewport at
        // once: the theme chip below is clicked where it lies rather than
        // scrolled to.
        prefsWin->setHeight(900);
        QVERIFY(QTest::qWaitForWindowExposed(prefsWin));
        prefsWin->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();

        auto option = [&](const char *name) {
            return findItem(prefsWin->contentItem(), name);
        };
        auto selected = [&](const char *name) {
            QQuickItem *row = option(name);
            return row && row->property("selected").toBool();
        };

        // Nothing stored yet, so the dots show the defaults tacky is applying.
        QVERIFY(option("autofetch_everyone"));
        QVERIFY(selected("autofetch_everyone"));
        QVERIFY(!selected("autofetch_contacts"));
        QVERIFY(selected("autofetchMax_5242880"));

        QVERIFY(QMetaObject::invokeMethod(option("autofetch_contacts"), "clicked"));
        QCOMPARE(app->settings()->attachmentAutofetch(), QString("contacts"));
        QVERIFY(selected("autofetch_contacts"));
        QVERIFY(!selected("autofetch_everyone"));

        // No cap is a cap of 0, which is picked like any other value.
        QVERIFY(QMetaObject::invokeMethod(option("autofetchMax_0"), "clicked"));
        QCOMPARE(app->settings()->attachmentAutofetchMax(), 0LL);
        QVERIFY(selected("autofetchMax_0"));
        QVERIFY(!selected("autofetchMax_5242880"));

        // The chips mark the theme in force, and a tap on one picks it.
        auto *appTheme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(appTheme);
        appTheme->setProperty("name", "plum");
        QCoreApplication::processEvents();
        QQuickItem *plum = option("theme_plum");
        QQuickItem *midnight = option("theme_midnight");
        QVERIFY(plum);
        QVERIFY(midnight);
        QVERIFY(plum->property("current").toBool());
        QVERIFY(!midnight->property("current").toBool());

        const QPoint at = prefsWin->contentItem()
                              ->mapFromItem(midnight, QPointF(midnight->width() / 2,
                                                              midnight->height() / 2))
                              .toPoint();
        QVERIFY2(prefsWin->contentItem()->boundingRect().contains(at),
                 qPrintable(QString("theme chip at %1,%2 in a %3x%4 viewport")
                                .arg(at.x()).arg(at.y())
                                .arg(prefsWin->contentItem()->width())
                                .arg(prefsWin->contentItem()->height())));
        QTest::mouseClick(prefsWin, Qt::LeftButton, Qt::NoModifier, at);
        QTRY_COMPARE(appTheme->property("name").toString(), QString("midnight"));
        QVERIFY(midnight->property("current").toBool());
        QVERIFY(!plum->property("current").toBool());

        // App-wide settings, so every window's menu leads to the same one.
        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first, again;
        QVERIFY(QMetaObject::invokeMethod(mgr, "preferences",
                                          Q_RETURN_ARG(QVariant, first)));
        QVERIFY(first.value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(mgr, "preferences",
                                          Q_RETURN_ARG(QVariant, again)));
        QCOMPARE(again.value<QObject *>(), first.value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(first.value<QObject *>(), "close"));
        QCoreApplication::processEvents();

        prefs.reset();
        QCoreApplication::processEvents();

        e.assertNoErrors();
    }

    // Opens one account's settings window into `holder`. Taller than the
    // shipped 760 so every card is in the viewport at once: what is scrolled
    // out is still there to find, but a synthesized drag needs its target on
    // screen.
    static QQuickWindow *openAccountSettings(QQmlEngine &e,
                                             QScopedPointer<QObject> &holder) {
        QQmlComponent comp(&e, "Quack", "AccountSettingsWindow");
        if (!comp.isReady()) {
            qWarning("%s", qPrintable(comp.errorString()));
            return nullptr;
        }
        holder.reset(comp.createWithInitialProperties({{"account", "me@example.com"}}));
        auto *w = qobject_cast<QQuickWindow *>(holder.data());
        if (!w)
            return nullptr;
        w->setHeight(1400);
        w->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();
        return w;
    }

    // The OMEMO rows the backend would have sent: this device, its fingerprint,
    // and three others in three different states of trust. They come from the
    // same model the backend feeds, so canned rows are enough.
    static AccountSettings *seedDevices(AppController *app) {
        AccountSettings *settings = app->accountSettingsFor("me@example.com");
        if (!settings)
            return nullptr;
        settings->devices()->applyOwnDevice(7);
        settings->devices()->applyOwnFingerprint(QString(64, QChar('b')));
        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "undecided"),
                                             device(9, "untrusted")});
        QCoreApplication::processEvents();
        return settings;
    }

    static void publishedAvatar(AppController *app, const QString &hash) {
        app->avatars()->handleEvent("avatar", "Update",
                                    QVariantMap{{"acc", "me@example.com"},
                                                {"jid", "me@example.com"},
                                                {"hash", hash}});
        QCoreApplication::processEvents();
    }

    void accountSettingsShowsTheStoredCredentials() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);

        AccountSettings *settings = app->accountSettingsFor("me@example.com");
        QVERIFY(settings);
        settings->applyAccount(QVariantMap{{"password", "hunter2"}});
        settings->applyNick("Kitsunia");
        QCoreApplication::processEvents();

        QObject *password = w->findChild<QObject *>("passwordField");
        QVERIFY(password);
        QCOMPARE(password->property("text").toString(), QString("hunter2"));
        QObject *nick = w->findChild<QObject *>("nickField");
        QVERIFY(nick);
        QCOMPARE(nick->property("text").toString(), QString("Kitsunia"));

        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first;
        QVERIFY(QMetaObject::invokeMethod(mgr, "accountSettings",
                                          Q_RETURN_ARG(QVariant, first),
                                          Q_ARG(QVariant, QVariant("me@example.com"))));
        QVariant again;
        QVERIFY(QMetaObject::invokeMethod(mgr, "accountSettings",
                                          Q_RETURN_ARG(QVariant, again),
                                          Q_ARG(QVariant, QVariant("me@example.com"))));
        oneWindowNotTwo(first, again);

        // Tear the page down inside the test rather than at the end of scope,
        // so anything its destruction logs is still collected. Note this does
        // not reproduce the delegate teardown seen in the running app.
        holder.reset();
        QCoreApplication::processEvents();

        e.assertNoErrors();
    }

    // The picture itself is the control, and removing is a second action on the
    // same chip - offered only once there is something to remove.
    void theAvatarOffersRemovalOnlyWithSomethingToRemove() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);

        QQuickItem *editor = findItem(w->contentItem(), "avatarEditor");
        QVERIFY(editor);

        // The chip that says the picture is tappable sits within its bounds
        // rather than off the edge of it.
        QQuickItem *setButton = findItem(editor, "avatarSetButton");
        QVERIFY(setButton);
        QVERIFY(setButton->property("visible").toBool());
        QVERIFY(editor->boundingRect().contains(itemRect(setButton, editor)));

        QQuickItem *remove = findItem(editor, "avatarRemoveButton");
        QVERIFY(remove);
        QVERIFY(!remove->property("visible").toBool()); // nothing published yet

        publishedAvatar(app, "abc123");
        w->grabWindow(); // the chip grew by an action; let the Row place it
        QVERIFY(remove->property("visible").toBool());
        QVERIFY(editor->boundingRect().contains(itemRect(remove, editor)));
        // Side by side on the chip: two separate targets, not one that moves.
        QVERIFY2(!itemRect(setButton, editor).intersects(itemRect(remove, editor)),
                 "the avatar's two actions overlap");

        // And it goes away again when the avatar does.
        publishedAvatar(app, "");
        QVERIFY(!remove->property("visible").toBool());

        e.assertNoErrors();
    }

    // A build with no QImage side refuses the picture instead of sending it,
    // and the refusal reaches the page rather than leaving it looking busy.
    void anAvatarWithNoEncoderIsRefusedOnThePage() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);

        AccountSettings *settings = app->accountSettingsFor("me@example.com");
        QVERIFY(settings);
        QObject *avatarStatus = w->findChild<QObject *>("avatarStatus");
        QVERIFY(avatarStatus);
        QVERIFY(!avatarStatus->property("visible").toBool()); // nothing asked yet

        settings->setAvatar(QUrl::fromLocalFile("/tmp/whatever.png"));
        QCoreApplication::processEvents();
        QVERIFY(!settings->avatarBusy());
        QVERIFY(avatarStatus->property("visible").toBool());
        QVERIFY(!avatarStatus->property("text").toString().isEmpty());

        e.assertNoErrors();
    }

    // With an encoder the publish goes out, and the picture stops taking taps
    // until it answers - along with the chip that advertises them.
    void publishingAnAvatarLocksThePictureAndNarratesIt() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);

        AccountSettings *settings = app->accountSettingsFor("me@example.com");
        QVERIFY(settings);
        QObject *avatarTap = w->findChild<QObject *>("avatarTap");
        QObject *avatarStatus = w->findChild<QObject *>("avatarStatus");
        QQuickItem *setButton = findItem(w->contentItem(), "avatarSetButton");
        QVERIFY(avatarTap);
        QVERIFY(avatarStatus);
        QVERIFY(setButton);
        QVERIFY(avatarTap->property("enabled").toBool());

        StubEncoder encoder;
        settings->setAvatarEncoder(&encoder);
        settings->setAvatar(QUrl::fromLocalFile("/tmp/whatever.png"));
        QVERIFY(settings->avatarBusy());
        QVERIFY(!avatarTap->property("enabled").toBool());
        QVERIFY(!setButton->property("visible").toBool());
        QCOMPARE(avatarStatus->property("text").toString(), QString("Publishing"));

        // tacky narrates the upload while it is out; the same line shows it.
        settings->handleEvent("avatar", "Progress",
                              QVariantMap{{"acc", "me@example.com"},
                                          {"message", "Updating metadata..."}});
        QCOMPARE(avatarStatus->property("text").toString(),
                 QString("Updating metadata..."));

        // No interpreter behind this window, so that request never reached a
        // wire; the backend answers it rather than leave the page waiting.
        QCoreApplication::processEvents();
        QVERIFY(!settings->avatarBusy());

        e.assertNoErrors();
    }

    // Set-all is offered while there is more than one device left to set, and
    // this device is never one of the rows.
    void theDeviceListLeavesOutOurOwnDevice() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);
        AccountSettings *settings = seedDevices(app);
        QVERIFY(settings);

        QObject *list = w->findChild<QObject *>("deviceList");
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 2);

        QObject *setAll = w->findChild<QObject *>("setAllRow");
        QVERIFY(setAll);
        QVERIFY(setAll->property("visible").toBool()); // two settable devices

        // One left to set, so there is nothing to set them all to.
        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "undecided"),
                                             device(9, "compromised")});
        QCoreApplication::processEvents();
        QVERIFY(!setAll->property("visible").toBool());

        e.assertNoErrors();
    }

    // Every trust control sits on the page's centre line, set-all included, so
    // they line up with each other as well.
    void theTrustPickersShareAColumn() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);
        QVERIFY(seedDevices(app));

        QQuickItem *setAllPicker = findItem(w->contentItem(), "setAllPicker");
        QQuickItem *devicePicker = findItem(w->contentItem(), "devicePicker");
        QVERIFY(setAllPicker);
        QVERIFY(devicePicker);
        const qreal apart = setAllPicker->mapToScene(QPointF(0, 0)).x()
                            - devicePicker->mapToScene(QPointF(0, 0)).x();
        QVERIFY2(qAbs(apart) <= 1.0,
                 qPrintable(QString("the two pickers start %1 apart").arg(apart)));
        QCOMPARE(setAllPicker->width(), devicePicker->width());

        QQuickItem *pickerRow = devicePicker->parentItem();
        QVERIFY(pickerRow);
        const qreal offCentre = devicePicker->x() + devicePicker->width() / 2
                                - pickerRow->width() / 2;
        QVERIFY2(qAbs(offCentre) <= 1.0,
                 qPrintable(QString("picker is %1 off centre").arg(offCentre)));

        e.assertNoErrors();
    }

    // The thumb settles under the segment the device's trust names, slides when
    // that changes under it, and picks the segment it is dragged onto.
    void theTrustThumbFollowsTheDeviceAndTakesADrag() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);
        AccountSettings *settings = seedDevices(app);
        QVERIFY(settings);

        QQuickItem *devicePicker = findItem(w->contentItem(), "devicePicker");
        QVERIFY(devicePicker);
        const qreal seg = devicePicker->property("segmentWidth").toReal();
        const qreal inset = devicePicker->property("inset").toReal();
        QVERIFY(seg > 0);
        QQuickItem *thumb = findItem(devicePicker, "trustThumb");
        QVERIFY(thumb);
        // Device 8 is undecided, the middle of the three.
        QTRY_COMPARE(thumb->x(), inset + seg);

        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "trusted"),
                                             device(9, "untrusted")});
        QTRY_COMPARE(thumb->x(), inset);

        const QPoint from = w->contentItem()
                                ->mapFromItem(thumb, QPointF(thumb->width() / 2,
                                                             thumb->height() / 2))
                                .toPoint();
        const QPoint to = from + QPoint(qRound(seg), 0);
        QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 8; ++step)
            QTest::mouseMove(w, from + QPoint(qRound(seg * step / 8.0), 0));
        QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_COMPARE(thumb->x(), inset + seg);

        e.assertNoErrors();
    }

    // A fingerprint spreads its groups over the width it is given, and each row
    // starts on the same column boundaries as the one above it.
    void aFingerprintWrapsOnItsColumns() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openAccountSettings(e, holder);
        QVERIFY(w);
        QVERIFY(seedDevices(app));

        QQuickItem *fpGrid = findItem(w->contentItem(), "fingerprintGroups");
        QVERIFY(fpGrid);
        QList<QQuickItem *> groups;
        const auto cells = fpGrid->childItems();
        for (QQuickItem *cell : cells)
            if (cell->objectName() == "fingerprintGroup")
                groups << cell;
        std::sort(groups.begin(), groups.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->y() != b->y() ? a->y() < b->y() : a->x() < b->x();
        });
        const int columns = fpGrid->property("columns").toInt();
        QVERIFY2(columns > 0 && columns < groups.size(),
                 qPrintable(QString("%1 groups over %2 columns never wrap")
                                .arg(groups.size())
                                .arg(columns)));
        QCOMPARE(groups.at(columns)->x(), groups.at(0)->x());
        QVERIFY(groups.at(columns)->y() > groups.at(0)->y());

        e.assertNoErrors();
    }

    // The same account twice is one window, not two stacked on each other.

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
        e.loadFromModule("Quack", "Main"); // ShellWindow arms AppWindows
        QVERIFY(!e.rootObjects().isEmpty());

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
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());

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

    // The mirror of the account page: who the roster says the contact is. The
    // identity card is read through the chat list rather than handed over, so
    // one opened before the list loaded fills in once it has.
    void contactDetailsFillsInWhenTheRosterArrives() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openContactDetails(e, holder);
        QVERIFY(w);

        QObject *standing = w->findChild<QObject *>("contactStanding");
        QObject *sharing = w->findChild<QObject *>("contactSharing");
        QObject *shownName = w->findChild<QObject *>("contactName");
        QVERIFY(standing);
        QVERIFY(sharing);
        QVERIFY(shownName);
        QCOMPARE(standing->property("text").toString(),
                 QString("Not in your contacts"));
        QCOMPARE(w->findChild<QObject *>("contactJid")->property("text").toString(),
                 QString("friend@example.com"));

        rosterEntry(app, "to", QString());
        QCOMPARE(standing->property("text").toString(), QString("In your contacts"));
        QCOMPARE(sharing->property("text").toString(),
                 QString("You see their status. They cannot see yours."));
        // The roster's name wins over the one the chat was opened under: a
        // rename lands here first.
        QCOMPARE(shownName->property("text").toString(), QString("Friend Renamed"));

        e.assertNoErrors();
    }

    // A request out and nothing back yet is its own state, not "no sharing" -
    // the entry carries `ask` alongside the subscription.
    void aPendingRequestIsItsOwnSharingState() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openContactDetails(e, holder);
        QVERIFY(w);

        rosterEntry(app, "none", "subscribe");
        QObject *sharing = w->findChild<QObject *>("contactSharing");
        QVERIFY(sharing);
        QCOMPARE(sharing->property("text").toString(),
                 QString("Waiting for them to approve your request."));

        e.assertNoErrors();
    }

    // The same devices model as the account's own panel, pointed at the contact
    // instead: every device they have is listed, and none of it is ours.
    void contactDetailsShowsTheirDevicesAndOurOwnKey() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openContactDetails(e, holder);
        QVERIFY(w);

        // Two models: the contact's devices, and this account's own key for the
        // other half of a comparison.
        const auto models = w->findChildren<OmemoDevicesModel *>();
        QCOMPARE(models.size(), 2);
        OmemoDevicesModel *devices = nullptr;
        OmemoDevicesModel *own = nullptr;
        for (OmemoDevicesModel *m : models)
            (m->jid() == QLatin1String("friend@example.com") ? devices : own) = m;
        QVERIFY(devices);
        QVERIFY(own);
        QCOMPARE(own->jid(), QString("me@example.com"));

        own->applyOwnFingerprint(QString(64, QChar('c')));
        QCoreApplication::processEvents();
        QQuickItem *ownFp = findItem(w->contentItem(), "ownFingerprint");
        QVERIFY(ownFp);
        QVERIFY(ownFp->property("visible").toBool());

        QObject *notice = w->findChild<QObject *>("noKeysNotice");
        QVERIFY(notice);
        QVERIFY(notice->property("visible").toBool()); // nothing known yet

        devices->applyTrustList({device(7, "trusted"), device(8, "undecided"),
                                 device(9, "untrusted")});
        QCoreApplication::processEvents();
        QObject *list = w->findChild<QObject *>("deviceList");
        QVERIFY(list);
        // Every one of theirs, including the id our own account happens to use:
        // the exclusion is about our own device, and this is not our account.
        QCOMPARE(list->property("count").toInt(), 3);
        QVERIFY(!notice->property("visible").toBool());
        QVERIFY(findItem(w->contentItem(), "devicePicker"));
        QObject *setAll = w->findChild<QObject *>("setAllRow");
        QVERIFY(setAll);
        QVERIFY(setAll->property("visible").toBool());

        // Blind trust is account-wide, so it stays on the account's page - a
        // per-contact panel is the wrong place to turn it off for everyone.
        QVERIFY(!findItem(w->contentItem(), "blindTrustBox"));

        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first, again;
        QVERIFY(QMetaObject::invokeMethod(mgr, "contactDetails", Q_RETURN_ARG(QVariant, first),
                                          Q_ARG(QVariant, QVariant("me@example.com")),
                                          Q_ARG(QVariant, QVariant("friend@example.com")),
                                          Q_ARG(QVariant, QVariant("Friend"))));
        QVERIFY(QMetaObject::invokeMethod(mgr, "contactDetails", Q_RETURN_ARG(QVariant, again),
                                          Q_ARG(QVariant, QVariant("me@example.com")),
                                          Q_ARG(QVariant, QVariant("friend@example.com")),
                                          Q_ARG(QVariant, QVariant("Friend"))));
        oneWindowNotTwo(first, again);

        e.assertNoErrors();
    }

    // A page with no room yet, which is how one built inside a chat's sheet sits
    // until somebody opens it. The card drawing our own row is hidden then - but
    // a hidden item's bindings run all the same, and a JID built by hand out of
    // two empty halves came out as "/", which tacky answers by throwing rather
    // than by missing.
    void aMucPageWithNoRoomBuildsNoJidOutOfNothing() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));

        QQmlComponent comp(&e, "Quack", "MucDetailsWindow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> blank(comp.createWithInitialProperties(
            {{"account", "me@example.com"}, {"jid", ""}}));
        QVERIFY(!blank.isNull());
        auto *blankWin = qobject_cast<QQuickWindow *>(blank.data());
        QVERIFY(blankWin);
        blankWin->grabWindow();
        QCoreApplication::processEvents();

        QQuickItem *blankAvatar = findItem(blankWin->contentItem(), "myAvatar");
        QVERIFY(blankAvatar);
        QCOMPARE(blankAvatar->property("jid").toString(), QString());
        QVERIFY(QMetaObject::invokeMethod(blank.data(), "close"));
        QCoreApplication::processEvents();

        e.assertNoErrors();
    }

    // The page is handed the chat JID and cuts the suffix off itself; until the
    // room answers, it says which of the three empty rooms this is.
    void anUnjoinedRoomSaysWhichEmptyRoomItIs() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);

        auto *room = w->findChild<MucRoomModel *>();
        QVERIFY(room);
        QCOMPARE(room->roomJid(), QString("room@muc.example.com"));

        QVERIFY(findItem(w->contentItem(), "occupantList"));
        QObject *empty = w->findChild<QObject *>("emptyNotice");
        QVERIFY(empty);
        QVERIFY(empty->property("visible").toBool());
        QObject *notJoined = w->findChild<QObject *>("notJoinedChip");
        QVERIFY(notJoined);
        QVERIFY(notJoined->property("visible").toBool());

        QVERIFY(joinedRoom(w));
        QVERIFY(!empty->property("visible").toBool());
        QVERIFY(!notJoined->property("visible").toBool());

        e.assertNoErrors();
    }

    // One heading per role present, in the model's order. Through QTRY, because
    // the section items are built on the view's next polish rather than when the
    // rows land.
    void occupantsAreHeadedByRoleInTheModelsOrder() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);
        QVERIFY(joinedRoom(w));

        QQuickItem *list = findItem(w->contentItem(), "occupantList");
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 3);

        const auto textsOf = [&](const QString &name, const char *prop) {
            QStringList out;
            for (QQuickItem *i : findItems(w->contentItem(), name))
                out << i->property(prop).toString();
            return out;
        };
        QTRY_COMPARE(textsOf("groupHeading", "section"),
                     QStringList({"moderator", "participant", "visitor"}));
        QTRY_COMPARE(textsOf("occupantNick", "text"),
                     QStringList({"mo", "amy", "zoe"}));

        e.assertNoErrors();
    }

    // Under each nick: their real address where the room discloses one, and
    // otherwise their own status text, quoted so it does not read as a word this
    // app chose. Nothing at all where there is neither - a presence word here
    // would sit beside somebody's "online" pretending to be one.
    void occupantsShowTheirAddressOrTheirOwnWords() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);
        QVERIFY(joinedRoom(w));

        // The rows arrive on the view's next polish, hence QTRY.
        QTRY_COMPARE(findItems(w->contentItem(), "occupantSecondLine").size(), 3);
        const QList<QQuickItem *> lines =
            findItems(w->contentItem(), "occupantSecondLine");
        QCOMPARE(lines.at(0)->property("text").toString(),
                 QString("mo@elsewhere.example"));
        QCOMPARE(lines.at(1)->property("text").toString(),
                 QString(u"“online”"));
        QVERIFY(!lines.at(2)->isVisible());

        e.assertNoErrors();
    }

    void theRoomCardCarriesTheSubjectAndOurOwnNick() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);
        QVERIFY(joinedRoom(w));

        QObject *subject = w->findChild<QObject *>("roomSubject");
        QVERIFY(subject);
        QCOMPARE(subject->property("text").toString(),
                 QString("what this room is about"));
        QObject *myNick = w->findChild<QObject *>("myNick");
        QVERIFY(myNick);
        QCOMPARE(myNick->property("text").toString(), QString("amy"));
        // And with a room and a nick to hand, a whole JID.
        QQuickItem *myAvatar = findItem(w->contentItem(), "myAvatar");
        QVERIFY(myAvatar);
        QCOMPARE(myAvatar->property("jid").toString(),
                 QString("room@muc.example.com/amy"));

        e.assertNoErrors();
    }

    // Exactly one row is ours, and only the one we may act on offers a menu.
    void onlyOurOwnRowIsMarkedAndOnlyOneOffersAMenu() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);
        QVERIFY(joinedRoom(w));

        // The rows arrive on the view's next polish, hence QTRY.
        const auto shown = [&](const QString &name) {
            int n = 0;
            for (QQuickItem *i : findItems(w->contentItem(), name))
                if (i->isVisible())
                    ++n;
            return n;
        };
        QTRY_COMPARE(shown("selfChip"), 1);
        QTRY_COMPARE(shown("occupantMenuButton"), 1);

        e.assertNoErrors();
    }

    // The filter narrows the rows without touching the room, so the heading
    // still counts everyone in the group.
    void theOccupantFilterNarrowsTheRowsNotTheRoom() {
        Engine e;
        QVERIFY(e.singletonInstance<AppController *>("Quack", "App"));
        QScopedPointer<QObject> holder;
        QQuickWindow *w = openMucDetails(e, holder);
        QVERIFY(w);
        MucRoomModel *room = joinedRoom(w);
        QVERIFY(room);

        QQuickItem *list = findItem(w->contentItem(), "occupantList");
        QVERIFY(list);
        room->setFilter("z");
        QCoreApplication::processEvents();
        QCOMPARE(list->property("count").toInt(), 1);
        QCOMPARE(room->total(), 3);
        room->setFilter("");
        QCoreApplication::processEvents();
        QCOMPARE(list->property("count").toInt(), 3);

        // Both details windows are written from, not only read.
        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first, again;
        QVERIFY(QMetaObject::invokeMethod(
            mgr, "mucDetails", Q_RETURN_ARG(QVariant, first),
            Q_ARG(QVariant, QVariant("me@example.com")),
            Q_ARG(QVariant, QVariant("room@muc.example.com?join")),
            Q_ARG(QVariant, QVariant("The Room"))));
        QVERIFY(QMetaObject::invokeMethod(
            mgr, "mucDetails", Q_RETURN_ARG(QVariant, again),
            Q_ARG(QVariant, QVariant("me@example.com")),
            Q_ARG(QVariant, QVariant("room@muc.example.com?join")),
            Q_ARG(QVariant, QVariant("The Room"))));
        oneWindowNotTwo(first, again);

        e.assertNoErrors();
    }


    // Closing the window is not a way to walk out on a running call: it hangs
    // up, and only then does the row (and with it the window) go.
    void closingACallWindowHangsUp() {
        AppEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());

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
        e.loadFromModule("Quack", "Main");
        QVERIFY(!e.rootObjects().isEmpty());

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

    // Opening a room straight after a 1:1 asked for its session with the last
    // chat's kind and kept that answer, so the room drew a padlock - shut, at
    // that, since a setting never asked about reads back as tacky's default.
    void openingARoomAfterAOneToOneLeavesNoPadlock() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        app->accounts()->applyList({"me@example.com"});

        QQuickWindow win;
        win.resize(1000, 700);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AppShell");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"initialAccount", "me@example.com"},
             {"width", 1000},
             {"height", 700}}));
        auto *shell = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(shell);
        shell->setParentItem(win.contentItem());
        QVERIFY(shell->property("wide").toBool());

        QQuickItem *chat = findItem(shell, "chatPane");
        QVERIFY(chat);
        QQuickItem *lock = findItem(shell, "omemoToggle");
        QVERIFY(lock);
        // The room's counterpart to the padlock, and just as wrong in the other
        // kind of chat: there are no participants to a conversation of two.
        QQuickItem *people = findItem(shell, "roomDetailsButton");
        QVERIFY(people);

        const auto openChat = [&](const QString &jid, bool groupchat) {
            QVERIFY(QMetaObject::invokeMethod(shell, "openChat",
                                              Q_ARG(QVariant, QVariant(jid)),
                                              Q_ARG(QVariant, QVariant(jid)),
                                              Q_ARG(QVariant, QVariant(groupchat))));
        };

        // The padlock waits to be told. No backend is running here, so the
        // read errors and nothing settles it but the event below.
        const auto tellUs = [&](const QString &jid, bool on) {
            emit app->backend()->event(
                "omemo", "Enabled",
                QVariantMap{{"acc", "me@example.com"}, {"jid", jid},
                            {"value", on}});
        };

        openChat("friend@example.com", false);
        QVERIFY2(!lock->isVisible(), "a padlock was drawn before anyone said");
        tellUs("friend@example.com", true);
        QTRY_VERIFY(lock->isVisible());
        QVERIFY(!people->isVisible());

        openChat("room@example.com", true);
        QVERIFY2(!lock->isVisible(), "a room was offered OMEMO");
        QVERIFY(!chat->property("canEncrypt").toBool());
        QVERIFY(people->isVisible());

        // The other way about: a room must not take the 1:1's padlock with it.
        // The session is cached, so the answer it already had comes back too.
        openChat("friend@example.com", false);
        QVERIFY(lock->isVisible());
        QVERIFY(!people->isVisible());

        win.grabWindow();
        QCoreApplication::processEvents();
        e.assertNoErrors();
    }

    // Every colour in the active palette has to reach the property named after
    // it, and the failure is silent: a QML property whose name starts with "on"
    // followed by a capital reads as a signal handler, so `onAccent` never took
    // its initialiser and every icon drawn on an accent fill came out
    // default-constructed black.
    void themePropertiesCarryTheirPaletteColour() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palette = theme->property("p").toMap();
        QVERIFY2(!palette.isEmpty(), "the active palette is empty");

        for (auto it = palette.cbegin(); it != palette.cend(); ++it) {
            const QColor want = QColor::fromString(it.value().toString());
            QVERIFY2(want.isValid(),
                     qPrintable(it.key() + " is not a colour: " + it.value().toString()));
            const QVariant got = theme->property(it.key().toUtf8().constData());
            QVERIFY2(got.isValid(),
                     qPrintable("Theme has no property for palette key " + it.key()));
            QVERIFY2(got.value<QColor>() == want,
                     qPrintable(QStringLiteral("Theme.%1 is %2, palette says %3")
                                    .arg(it.key(), got.value<QColor>().name(),
                                         want.name())));
        }
    }

    // Adding a colour means editing every palette block, and missing one only
    // fails when somebody switches to that theme - at which point the property
    // reads as an invalid colour and whatever it painted comes out black. So
    // check the key sets match rather than trusting nine hand edits.
    void everyPaletteCarriesTheSameKeys() {
        Engine e;
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        const QVariantMap palettes = theme->property("palettes").toMap();
        QVERIFY2(palettes.size() > 1, "expected several palettes to compare");

        // QVariantMap keys come back sorted, so this compares as sets.
        const QStringList want = palettes.first().toMap().keys();
        QVERIFY(!want.isEmpty());
        for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
            const QStringList got = it.value().toMap().keys();
            QVERIFY2(got == want,
                     qPrintable(QStringLiteral("palette \"%1\" has [%2], expected [%3]")
                                    .arg(it.key(), got.join(", "), want.join(", "))));
        }
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

    // The device list is built from a plain JS array that gets replaced whole
    // every time devices are re-enumerated, so the menu is filled by an
    // Instantiator rather than declared. Check it really populates, that the
    // tick tracks the current id rather than a position, and that picking
    // reports the id instead of the label.
    void deviceMenuListsEveryDeviceAndReportsThePick() {
        Engine e;
        QQuickWindow win;
        win.resize(400, 120);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AudioLevelRow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        auto *row = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
            {{"label", "Microphone"},
             {"devices",
              QVariantList{QVariantMap{{"name", "Built-in"}, {"id", "mic-1"}},
                           QVariantMap{{"name", "USB headset"}, {"id", "mic-2"}}}},
             {"deviceId", "mic-2"},
             {"width", win.width()}}));
        QVERIFY2(row, qPrintable(comp.errorString()));
        row->setParentItem(win.contentItem());

        QObject *menu = row->findChild<QObject *>("deviceMenu");
        QVERIFY(menu);
        // The two real devices, behind the system-default entry.
        QTRY_COMPARE(menu->property("count").toInt(), 3);

        auto itemAt = [&](int i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, i));
            return item;
        };
        QVERIFY(itemAt(2));
        QVERIFY(!itemAt(0)->property("current").toBool());
        QVERIFY(!itemAt(1)->property("current").toBool());
        QVERIFY(itemAt(2)->property("current").toBool());

        QSignalSpy picked(row, SIGNAL(devicePicked(QString)));
        QVERIFY(QMetaObject::invokeMethod(itemAt(1), "triggered"));
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.first().at(0).toString(), QString("mic-1"));

        // "" is a real choice, not an empty one: it hands the pick back to the
        // system rather than leaving the device alone.
        QVERIFY(QMetaObject::invokeMethod(itemAt(0), "triggered"));
        QCOMPARE(picked.count(), 2);
        QCOMPARE(picked.at(1).at(0).toString(), QString());

        e.assertNoErrors();
    }

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

    // Joining a room is a bookmark write with autojoin set - that is what keeps
    // tacky rejoining it - and the typed nick and password have to ride along.
    void joinRoomWritesABookmark() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);

        QQuickWindow win;
        win.resize(480, 600);
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

        QObject *sheet = page->findChild<QObject *>("joinRoomSheet");
        QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
        // The service box is a guess at where this server keeps its rooms.
        QCOMPARE(sheet->findChild<QObject *>("joinRoomService")->property("text").toString(),
                 QString("conference.example.com"));

        QSignalSpy sent(app->backend(), &TackyBackend::sent);
        sheet->findChild<QObject *>("joinRoomJid")
            ->setProperty("text", "Room@Conference.Example.com");
        sheet->findChild<QObject *>("joinRoomNick")->setProperty("text", "romeo");
        sheet->findChild<QObject *>("joinRoomPassword")->setProperty("text", "s3cret");
        QVERIFY(QMetaObject::invokeMethod(sheet, "accept"));

        QCOMPARE(sent.count(), 1);
        QCOMPARE(sent.at(0).at(0).toString(), QString("bookmarks"));
        QCOMPARE(sent.at(0).at(1).toString(), QString("item"));
        const QVariantMap args = sent.at(0).at(2).toMap();
        QCOMPARE(args.value("jid").toString(),
                 QString("room@conference.example.com"));
        QCOMPARE(args.value("nick").toString(), QString("romeo"));
        QCOMPARE(args.value("password").toString(), QString("s3cret"));
        QCOMPARE(args.value("autojoin").toInt(), 1);

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

    // Android lays the keyboard over the window instead of resizing it, so a
    // dialog centres in the strip left above it. The rectangle Android reports
    // is in physical pixels, and clamped against a window measured in
    // device-independent ones it read as the whole window - leaving the sheet
    // dead centre under the keyboard. Nothing raises a keyboard on a headless
    // platform, so the top edge is placed by hand.
    void aDialogCentresInTheStripTheKeyboardLeaves() {
        Engine e;

        QQuickWindow win;
        win.resize(360, 800);
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

        QObject *sheet = rail->findChild<QObject *>("addAccountSheet");
        QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
        QTRY_VERIFY(sheet->property("opened").toBool());

        const qreal h = sheet->property("height").toReal();
        // Short enough that a keyboard over the bottom half still leaves a
        // strip it fits in, or the top margin is what would be measured below.
        QVERIFY2(h > 0 && h < 380, qPrintable(QString("sheet is %1 high").arg(h)));

        // Keyboard down: dead centre of the window.
        QCOMPARE(sheet->property("keyboardTop").toReal(), qreal(-1));
        QTRY_COMPARE(sheet->property("y").toReal(),
                     qreal(qRound((win.height() - h) / 2)));

        // Keyboard up over the bottom half: centred in what is left of the
        // window, and wholly above the keyboard.
        sheet->setProperty("keyboardTop", 400);
        QTRY_COMPARE(sheet->property("y").toReal(), qreal(qRound((400 - h) / 2)));
        QVERIFY(sheet->property("y").toReal() + h <= 400);

        // A strip too short to hold it: pinned under the top margin rather than
        // centred off the top of the window.
        sheet->setProperty("keyboardTop", 40);
        QTRY_COMPARE(sheet->property("y").toReal(), qreal(12));

        e.assertNoErrors();
    }

    // A registration form is drawn from questions the app has never seen: the
    // field types decide which control answers each one, and nothing about the
    // page can be checked against a layout written for it. So: every field got
    // a control, of the kind its type calls for, laid out down the page.
    void drawsWhateverFormTheServerAsked() {
        Engine e;
        e.singletonInstance<AppController *>("Quack", "App");

        QQuickWindow win;
        win.resize(360, 600);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        // Declared before the form it feeds, so it outlives it.
        TackyBackend backend;
        RegistrationController reg;
        reg.setBackend(&backend); // unstarted: a read gets a token and no reply
        reg.applyForm(QJsonDocument::fromJson(R"({
            "instructions":"Pick a name",
            "fields":[
                {"var":"FORM_TYPE","type":"hidden","value":["jabber:iq:register"]},
                {"var":"username","type":"text-single","label":"Username",
                 "required":true,"value":[]},
                {"var":"password","type":"text-private","label":"Password",
                 "required":true,"value":[]},
                {"var":"tier","type":"list-single","label":"Tier","value":["paid"],
                 "options":[{"label":"Free","value":"free"},
                            {"label":"Paid","value":"paid"}]},
                {"var":"terms","type":"boolean","label":"I agree","value":["0"]},
                {"var":"ocr","type":"text-single","label":"Type the word",
                 "required":true,"value":[],
                 "media":{"cid":"c1","type":"image/png"}}
            ]})")
                          .object()
                          .toVariantMap());
        QCOMPARE(reg.rowCount(), 5);

        QQmlComponent comp(&e, "Quack", "DataForm");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"fields", QVariant::fromValue(&reg)}, {"width", win.width()}}));
        QVERIFY(!obj.isNull());
        auto *form = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(form);
        form->setParentItem(win.contentItem());
        win.grabWindow(); // force the delegates to lay out and bind
        QCoreApplication::processEvents();

        // The control each type calls for, and no other: a row that builds one
        // of each and shows one is how a delegate covers six field types.
        QQuickItem *user = findItem(win.contentItem(), "formLine0");
        QQuickItem *secret = findItem(win.contentItem(), "formLine1");
        QQuickItem *tier = findItem(win.contentItem(), "formChoice2");
        QQuickItem *terms = findItem(win.contentItem(), "formTick3");
        QVERIFY(user && secret && tier && terms);
        QVERIFY(user->isVisible() && secret->isVisible());
        QVERIFY(tier->isVisible() && terms->isVisible());
        QVERIFY(!findItem(win.contentItem(), "formChoice0")->isVisible());
        // The one the password goes in is the one that hides it.
        QCOMPARE(secret->property("echoMode").toInt(), 2); // TextInput.Password
        // A list field opens on what the server preselected.
        QCOMPARE(tier->property("currentText").toString(), QString("Paid"));

        // Laid out down the page, each inside the form and none over another:
        // a form that stacked its fields at zero height would still find every
        // control above and read as drawn.
        QRectF last;
        for (const QString &name :
             {"formLine0", "formLine1", "formChoice2", "formTick3", "formLine4"}) {
            QQuickItem *item = findItem(win.contentItem(), name);
            QVERIFY2(item, qPrintable(name));
            const QRectF r = itemRect(item, form);
            QVERIFY2(r.width() > 0 && r.height() > 0, qPrintable(name));
            QVERIFY2(r.left() >= 0 && r.right() <= form->width() + 1, qPrintable(name));
            QVERIFY2(r.top() >= last.bottom(), qPrintable(name));
            last = r;
        }
        QVERIFY(form->height() >= last.bottom());

        // Typing an answer writes it back through the model, which is where a
        // form's state lives: nothing is ever collected off the controls.
        user->forceActiveFocus();
        for (const QChar c : QStringLiteral("alice"))
            QTest::keyClick(&win, c.toLatin1());
        QTRY_COMPARE(reg.valueFor("username"), QString("alice"));

        // The CAPTCHA is a further round trip, so its field is drawn before
        // there is a picture for it: nothing until the bytes turn up, then the
        // picture itself, straight out of a data: URL.
        QQuickItem *picture = findItem(win.contentItem(), "formMedia4");
        QVERIFY(picture);
        QVERIFY(!picture->isVisible());
        QSignalSpy sent(&backend, &TackyBackend::sent);
        reg.handleEvent("register", "MediaReady",
                        QVariantMap{{"token", reg.token()}, {"var", "ocr"}});
        QVERIFY(asked(sent, "register", "media"));
        // The first read this backend has been asked for, so token 1.
        reg.handleResult(1, QString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJ"
                                    "AAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5"
                                    "ErkJggg=="));
        QTRY_VERIFY(picture->isVisible());
        QTRY_COMPARE(picture->property("status").toInt(), 1); // Image.Ready
        QVERIFY(picture->width() > 0 && picture->height() > 0);

        e.assertNoErrors();
    }

    // The per-account models are cached on the App singleton, so nothing else
    // would ever let go of one for an account that has been removed.
    void removingAnAccountDropsWhatWasCachedForIt() {
        Engine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        app->accounts()->applyAdded("gone@example.com");
        QPointer<ChatListModel> chats = app->chatListFor("gone@example.com");
        QVERIFY(chats);

        app->accounts()->applyRemoved("gone@example.com");
        QTRY_VERIFY(chats.isNull());
    }
};

QTEST_MAIN(TestQmlLoad)
#include "tst_qmlload.moc"
