// Rooms: the details page, its occupants, and the bookmark a join writes.
#include <QtTest>
#include <QQmlComponent>

#include "AppController.h"
#include "MucRoomModel.h"
#include "TackyBackend.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestMuc : public QObject {
    Q_OBJECT

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

private slots:
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
};

QTEST_MAIN(TestMuc)
#include "tst_qmlload_muc.moc"
