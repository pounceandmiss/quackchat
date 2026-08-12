// Loads the app QML headless with the real registered types. Binding errors
// (ReferenceError etc.) only surface as warnings, so collect and fail on them.
#include <QtTest>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include "CallsModel.h"
#include "ChatListModel.h"
#include "OmemoDevicesModel.h"
#include "SearchModel.h"
#include "TackyBackend.h"

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

    // Results are drawn from several chats at once, each of which brings its
    // own name cache - the part of the page that only runs with rows in it.
    void drawsSearchResults() {
        QStringList warnings;
        QQmlEngine e;
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        QQuickWindow win;
        win.resize(360, 500);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "SearchPage");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"account", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *page = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(page);
        page->setParentItem(win.contentItem());

        auto *model = page->findChild<SearchModel *>();
        QVERIFY(model);
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

        QQuickItem *list = findItem(win.contentItem(), "searchResults");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 2);
        // One name cache per chat the results touch, built as they arrive.
        QTRY_COMPARE(page->property("authorsByChat").toMap().size(), 2);
        win.grabWindow(); // force the delegates to lay out and bind
        QCoreApplication::processEvents();
        assertNoQmlErrors(warnings);
    }

    // Searching inside a chat walks the hits in the feed instead of listing
    // them, so the step - and what the counter says about it - is the feature.
    void walksHitsInsideTheChat() {
        QStringList warnings;
        QQmlEngine e;
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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
        QTRY_VERIFY_WITH_TIMEOUT(model->searching(), 3000);
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
        assertNoQmlErrors(warnings);
    }

    // The bubble decides, from the attachment alone, whether a tap opens the
    // file or has to fetch it first - the same rule the Tk client draws on.
    void drawsAttachments() {
        QStringList warnings;
        QQmlEngine e;
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        // A real file on disk: the thumbnail's size is the decoded image's.
        // The '#' in the directory is why the model hands over a url and not a
        // path - "file:" concatenated onto this one loses everything after it.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir("od#d"));
        const QString thumbPath = dir.filePath("od#d/a_320.png");
        QImage png(24, 12, QImage::Format_RGB32);
        png.fill(Qt::red);
        QVERIFY(png.save(thumbPath));

        QQuickWindow win;
        win.resize(500, 400);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "ChatBubble");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));

        // As ChatModel hands them over: what the message said, merged with the
        // state of the transfer.
        auto attachment = [](const QString &type, const QString &name,
                             const QString &thumb, const QString &state) {
            return QVariantMap{
                {"url", "https://h/" + name},
                {"type", type},
                {"name", name},
                {"size", 2048},
                {"mime", ""},
                {"state", state},
                {"loaded", 0},
                {"total", 0},
                {"localpath", ""},
                {"thumburl", thumb.isEmpty() ? QUrl() : QUrl::fromLocalFile(thumb)},
                {"error", ""}};
        };
        // Width up front: the bubble sizes itself off its parent, and one built
        // parentless would fall back to measuring its own content instead.
        auto bubbleWith = [&](const QVariantMap &att) {
            auto *item = qobject_cast<QQuickItem *>(comp.createWithInitialProperties(
                {{"attachments", QVariantList{att}},
                 {"text", ""},
                 {"width", win.width()}}));
            if (item)
                item->setParentItem(win.contentItem());
            return item;
        };
        auto tap = [&](QQuickItem *target) {
            const QPoint p = win.contentItem()
                                 ->mapFromItem(target, QPointF(target->width() / 2,
                                                               target->height() / 2))
                                 .toPoint();
            QTest::mouseClick(&win, Qt::LeftButton, Qt::NoModifier, p);
        };

        // A downloaded image draws its thumbnail, and a tap opens it.
        {
            QScopedPointer<QQuickItem> b(
                bubbleWith(attachment("image", "a.png", thumbPath, "done")));
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
            tap(thumb);
            QCOMPARE(opened.count(), 1);
            QCOMPARE(opened.first().at(0).toInt(), 0);
            QCOMPARE(loaded.count(), 0);
        }

        // One nobody has fetched yet is a chip, and a tap fetches it.
        {
            QScopedPointer<QQuickItem> b(
                bubbleWith(attachment("image", "b.png", "", "")));
            QVERIFY(!b.isNull());
            QQuickItem *chip = findItem(b.data(), "attachmentChip");
            QVERIFY(chip);
            QVERIFY(chip->isVisible());
            QVERIFY(!findItem(b.data(), "attachmentThumb")->isVisible());

            QSignalSpy opened(b.data(), SIGNAL(attachmentOpenRequested(int)));
            QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
            tap(chip);
            QCOMPARE(loaded.count(), 1);
            QCOMPARE(opened.count(), 0);
        }

        // A plain file has no thumbnail to wait for: its tap opens, which
        // downloads first if it has to.
        {
            QScopedPointer<QQuickItem> b(
                bubbleWith(attachment("file", "doc.pdf", "", "")));
            QVERIFY(!b.isNull());
            QQuickItem *chip = findItem(b.data(), "attachmentChip");
            QVERIFY(chip);
            QVERIFY(chip->isVisible());

            QSignalSpy opened(b.data(), SIGNAL(attachmentOpenRequested(int)));
            tap(chip);
            QCOMPARE(opened.count(), 1);

            QVariant size;
            QVERIFY(QMetaObject::invokeMethod(b.data(), "fmtSize",
                                              Q_RETURN_ARG(QVariant, size),
                                              Q_ARG(QVariant, 2048)));
            QCOMPARE(size.toString(), QString("2.0 KB"));
        }

        // An image tacky held back, capped or cancelled ends `idle`: nothing on
        // disk and no error to report, so it draws the same tap-to-load chip as
        // one nobody has asked for.
        {
            QScopedPointer<QQuickItem> b(
                bubbleWith(attachment("image", "d.png", "", "idle")));
            QVERIFY(!b.isNull());
            QQuickItem *chip = findItem(b.data(), "attachmentChip");
            QVERIFY(chip);
            QVERIFY(chip->isVisible());
            QVERIFY(!findItem(b.data(), "attachmentProgress")->isVisible());

            QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
            tap(chip);
            QCOMPARE(loaded.count(), 1);
        }

        // A failed transfer retries rather than opening nothing.
        {
            QScopedPointer<QQuickItem> b(
                bubbleWith(attachment("file", "doc.pdf", "", "failed")));
            QVERIFY(!b.isNull());
            QSignalSpy loaded(b.data(), SIGNAL(attachmentLoadRequested(int)));
            tap(findItem(b.data(), "attachmentChip"));
            QCOMPARE(loaded.count(), 1);
        }

        assertNoQmlErrors(warnings);
    }

    void loadsAccountSettings() {
        QStringList warnings;
        QQmlEngine e;
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        QQmlComponent comp(&e, "Quack", "AccountSettingsWindow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> win(
            comp.createWithInitialProperties({{"account", "me@example.com"}}));
        QVERIFY(!win.isNull());
        auto *w = qobject_cast<QQuickWindow *>(win.data());
        QVERIFY(w);
        w->grabWindow(); // force a render so the cards' bindings evaluate
        QCoreApplication::processEvents();

        // The device rows come from the same model the backend feeds, so canned
        // rows are enough to check the list and the set-all rule are wired up.
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        AccountSettings *settings = app->accountSettingsFor("me@example.com");
        QVERIFY(settings);
        settings->devices()->applyOwnDevice(7);
        settings->devices()->applyOwnFingerprint(QString(64, QChar('b')));
        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "undecided"),
                                             device(9, "untrusted")});
        QCoreApplication::processEvents();

        // The form fields read the stored credential and name.
        settings->applyAccount(QVariantMap{{"password", "hunter2"}});
        settings->applyNick("Kitsunia");
        QCoreApplication::processEvents();
        QObject *password = w->findChild<QObject *>("passwordField");
        QVERIFY(password);
        QCOMPARE(password->property("text").toString(), QString("hunter2"));
        QObject *nick = w->findChild<QObject *>("nickField");
        QVERIFY(nick);
        QCOMPARE(nick->property("text").toString(), QString("Kitsunia"));

        QObject *list = w->findChild<QObject *>("deviceList");
        QVERIFY(list);
        QCOMPARE(list->property("count").toInt(), 2); // our own device is not one of them

        QObject *setAll = w->findChild<QObject *>("setAllRow");
        QVERIFY(setAll);
        QVERIFY(setAll->property("visible").toBool()); // two settable devices

        // Every trust control starts on the same edge, set-all included.
        QQuickItem *setAllPicker = findItem(w->contentItem(), "setAllPicker");
        QQuickItem *devicePicker = findItem(w->contentItem(), "devicePicker");
        QVERIFY(setAllPicker);
        QVERIFY(devicePicker);
        QCOMPARE(setAllPicker->mapToScene(QPointF(0, 0)).x(),
                 devicePicker->mapToScene(QPointF(0, 0)).x());
        QCOMPARE(setAllPicker->width(), devicePicker->width());

        // The thumb settles under the segment the device's trust names, and
        // slides when that changes under it. Device 8 is undecided, the middle
        // of the three.
        const qreal seg = devicePicker->property("segmentWidth").toReal();
        const qreal inset = devicePicker->property("inset").toReal();
        QVERIFY(seg > 0);
        QQuickItem *thumb = findItem(devicePicker, "trustThumb");
        QVERIFY(thumb);
        QTRY_COMPARE(thumb->x(), inset + seg);

        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "trusted"),
                                             device(9, "untrusted")});
        QTRY_COMPARE(thumb->x(), inset);

        // Dragging the thumb one segment along picks that one on release.
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

        // A fingerprint spreads its groups over the width it is given, and each
        // row starts on the same column boundaries as the one above it.
        QQuickItem *fpGrid = findItem(w->contentItem(), "fingerprintGroups");
        QVERIFY(fpGrid);
        QVERIFY(fpGrid->parentItem());
        QCOMPARE(fpGrid->width(), fpGrid->parentItem()->width());
        QList<QQuickItem *> groups;
        const auto cells = fpGrid->childItems();
        for (QQuickItem *cell : cells)
            if (cell->objectName() == "fingerprintGroup")
                groups << cell;
        QCOMPARE(groups.size(), 8); // 64 hex in groups of 8
        std::sort(groups.begin(), groups.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->y() != b->y() ? a->y() < b->y() : a->x() < b->x();
        });
        const int columns = fpGrid->property("columns").toInt();
        QVERIFY2(columns >= 4, qPrintable(QString("only %1 columns").arg(columns)));
        if (columns < groups.size()) {
            QCOMPARE(groups.at(columns)->x(), groups.at(0)->x());
            QVERIFY(groups.at(columns)->y() > groups.at(0)->y());
        }

        // The tick belongs on the left edge of its row; the style centres it
        // instead when the control carries no text of its own.
        QQuickItem *blindTrust = findItem(w->contentItem(), "blindTrustBox");
        QVERIFY(blindTrust);
        auto *tick = blindTrust->property("indicator").value<QQuickItem *>();
        QVERIFY(tick);
        QCOMPARE(tick->x(), 0.0);

        // One left to set, so there is nothing to set them all to.
        settings->devices()->applyTrustList({device(7, "trusted"), device(8, "undecided"),
                                             device(9, "compromised")});
        QCoreApplication::processEvents();
        QVERIFY(!setAll->property("visible").toBool());

        // The same account twice is one window, not two stacked on each other.
        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first;
        QVERIFY(QMetaObject::invokeMethod(mgr, "accountSettings",
                                          Q_RETURN_ARG(QVariant, first),
                                          Q_ARG(QVariant, QVariant("me@example.com"))));
        QVERIFY(first.value<QObject *>());
        QVariant again;
        QVERIFY(QMetaObject::invokeMethod(mgr, "accountSettings",
                                          Q_RETURN_ARG(QVariant, again),
                                          Q_ARG(QVariant, QVariant("me@example.com"))));
        QCOMPARE(again.value<QObject *>(), first.value<QObject *>());

        // AppWindows holds this one, so it would otherwise outlive the engine's
        // singletons and re-evaluate its bindings against them on the way down.
        // QQmlApplicationEngine drops its windows first.
        QVERIFY(QMetaObject::invokeMethod(first.value<QObject *>(), "close"));
        QCoreApplication::processEvents();

        assertNoQmlErrors(warnings);
    }

    // Call windows are never asked for: they follow a CallsModel row, with the
    // roles arriving as the delegate's required properties. This is the part
    // that no C++ test can reach, so drive canned events through the real
    // singleton and look at what is actually on screen.
    //
    // A ringing call is the dialog's, not the call window's - the window only
    // appears once there is a call in it, as tacky's Tk GUI does it.
    void ringingCallShowsTheDialogUntilItIsAnswered() {
        QStringList warnings;
        QQmlApplicationEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
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

        assertNoQmlErrors(warnings);
    }

    // Declining never shows a call window, so nothing is left holding the row
    // open: it has to clear itself or it sits in the model unseen forever.
    void decliningARingingCallClearsTheRow() {
        QStringList warnings;
        QQmlApplicationEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
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

        assertNoQmlErrors(warnings);
    }

    // The same devices model as the account's own panel, pointed at a contact
    // instead: every device they have is listed, and none of it is ours.
    void loadsOmemoKeysWindow() {
        QStringList warnings;
        QQmlEngine e;
        e.singletonInstance<AppController *>("Quack", "App");
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        QQmlComponent comp(&e, "Quack", "OmemoKeysWindow");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> win(
            comp.createWithInitialProperties({{"account", "me@example.com"},
                                              {"jid", "friend@example.com"},
                                              {"name", "Friend"}}));
        QVERIFY(!win.isNull());
        auto *w = qobject_cast<QQuickWindow *>(win.data());
        QVERIFY(w);
        w->grabWindow();
        QCoreApplication::processEvents();

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

        // One window per contact: a second view of the same trust state would
        // argue with the first over what is on screen.
        auto *mgr = e.singletonInstance<QObject *>("Quack", "AppWindows");
        QVERIFY(mgr);
        QVariant first, again;
        QVERIFY(QMetaObject::invokeMethod(mgr, "omemoKeys", Q_RETURN_ARG(QVariant, first),
                                          Q_ARG(QVariant, QVariant("me@example.com")),
                                          Q_ARG(QVariant, QVariant("friend@example.com")),
                                          Q_ARG(QVariant, QVariant("Friend"))));
        QVERIFY(first.value<QObject *>());
        QVERIFY(QMetaObject::invokeMethod(mgr, "omemoKeys", Q_RETURN_ARG(QVariant, again),
                                          Q_ARG(QVariant, QVariant("me@example.com")),
                                          Q_ARG(QVariant, QVariant("friend@example.com")),
                                          Q_ARG(QVariant, QVariant("Friend"))));
        QCOMPARE(again.value<QObject *>(), first.value<QObject *>());

        QVERIFY(QMetaObject::invokeMethod(first.value<QObject *>(), "close"));
        QCoreApplication::processEvents();

        assertNoQmlErrors(warnings);
    }

    // Closing the window is not a way to walk out on a running call: it hangs
    // up, and only then does the row (and with it the window) go.
    void closingACallWindowHangsUp() {
        QStringList warnings;
        QQmlApplicationEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
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

        assertNoQmlErrors(warnings);
    }

    // Calling one of your own accounts from another is one session but two
    // calls, and this app is on both ends of it. What is on screen is one call
    // window and one ringing dialog - never two call windows on the same spot,
    // which is unreadable - and, the bug this pins, one end finishing says
    // nothing about the other: a sibling device answering ends the callee end
    // while the caller end is up and audible.
    void bothEndsOfACallBetweenOwnAccountsShowOneWindowAndOneDialog() {
        QStringList warnings;
        QQmlApplicationEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlApplicationEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
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

        assertNoQmlErrors(warnings);
    }

    // Narrow, the account rail is not a column but a pull-out drawer: 64px of
    // permanent chrome is a sixth of a phone's width. The list header's ☰ is
    // the way in, back is the way out, and widening past the breakpoint puts
    // the rail back in the layout with nothing left for the drawer to show.
    void narrowLayoutMovesTheAccountRailIntoADrawer() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

        app->accounts()->applyList({"me@example.com", "alt@example.com"});
        // Both signed in: a disabled account says so instead of saying what
        // its connection is doing, which is not what is under test here.
        app->accounts()->applyEnabledList({"me@example.com", "alt@example.com"});
        app->accounts()->setConnState("me@example.com", "connected");
        app->accounts()->setConnState("alt@example.com", "conn-error");

        QQuickWindow win;
        win.resize(400, 700);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));

        QQmlComponent comp(&e, "Quack", "AppShell");
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        QScopedPointer<QObject> obj(comp.createWithInitialProperties(
            {{"initialAccount", "me@example.com"},
             {"width", win.width()},
             {"height", win.height()}}));
        QVERIFY(!obj.isNull());
        auto *shell = qobject_cast<QQuickItem *>(obj.data());
        QVERIFY(shell);
        shell->setParentItem(win.contentItem());
        QVERIFY(!shell->property("wide").toBool());

        // The inline rail is out of the layout, and the list header has picked
        // up the only way back to it.
        QQuickItem *inlineRail = findItem(shell, "accountRail");
        QVERIFY(inlineRail);
        QVERIFY(!inlineRail->isVisible());
        QQuickItem *hamburger = findItem(shell, "accountsButton");
        QVERIFY(hamburger);
        QVERIFY(hamburger->isVisible());

        // A Drawer hangs off the window overlay rather than the shell's own
        // items, so it is the QObject tree that finds it.
        auto *drawer = shell->findChild<QObject *>("accountDrawer");
        QVERIFY(drawer);
        QVERIFY(!drawer->property("opened").toBool());

        const QPoint tap = win.contentItem()
                               ->mapFromItem(hamburger,
                                             QPointF(hamburger->width() / 2,
                                                     hamburger->height() / 2))
                               .toPoint();
        QTest::mouseClick(&win, Qt::LeftButton, Qt::NoModifier, tap);
        QTRY_VERIFY2(drawer->property("opened").toBool(),
                     "the header button did not pull the drawer out");

        // The same component at its expanded density.
        auto *drawerRail = drawer->findChild<QQuickItem *>("accountRail");
        QVERIFY(drawerRail);
        QVERIFY(drawerRail->property("expanded").toBool());
        QVERIFY2(drawerRail->width() > 200,
                 qPrintable(QString("drawer rail is only %1 wide")
                                .arg(drawerRail->width())));
        QVERIFY(!inlineRail->property("expanded").toBool());

        // A Drawer sizes to its content, and this rail is built from anchors, so
        // it offers no implicit height. Left alone the drawer opens zero-height:
        // the list collapses, the column overflows it, and the ＋ button is the
        // only thing drawn - over nothing, the background having no height
        // either. Model counts read correct straight through that, so geometry
        // is what has to be asserted.
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

        QStringList states;
        const auto stateItems = findItems(drawerRail, "accountRowState");
        for (QQuickItem *item : stateItems)
            states << item->property("text").toString();
        std::sort(states.begin(), states.end());
        // In words, not just the dot's colour, which is red for both a rejected
        // password and an unreachable server.
        QCOMPARE(states, QStringList({"connected", "connection failed"}));

        // Android's back closes the drawer before it touches the navigation
        // underneath it.
        QVariant popped;
        QVERIFY(QMetaObject::invokeMethod(shell, "handleBack",
                                          Q_RETURN_ARG(QVariant, popped)));
        QVERIFY(popped.toBool());
        QTRY_VERIFY(!drawer->property("opened").toBool());

        // Picking an account is the whole errand, so it closes behind you.
        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(drawerRail, "selectAccount",
                                          Q_ARG(QString, QString("alt@example.com"))));
        QCOMPARE(shell->property("currentAccount").toString(),
                 QString("alt@example.com"));
        QTRY_VERIFY(!drawer->property("opened").toBool());

        // The header button is not the only way in; a drag from the left edge
        // pulls it out too. Whether Android's own edge gesture lets that touch
        // through before claiming it for Back is a system question no offscreen
        // test can answer - this pins the Qt half only.
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

        // Wide, the rail is a column again and the header's way in goes with it.
        shell->setWidth(1000);
        QTRY_VERIFY(shell->property("wide").toBool());
        QVERIFY(inlineRail->isVisible());
        QVERIFY(!hamburger->isVisible());

        // Growing while it is open leaves two copies of the rail on screen,
        // so the drawer has to let go on the way past the breakpoint.
        shell->setWidth(400);
        QTRY_VERIFY(!shell->property("wide").toBool());
        QVERIFY(QMetaObject::invokeMethod(drawer, "open"));
        QTRY_VERIFY(drawer->property("opened").toBool());
        shell->setWidth(1000);
        QTRY_VERIFY2(!drawer->property("opened").toBool(),
                     "the drawer stayed out after the rail came back inline");

        win.grabWindow();
        QCoreApplication::processEvents();
        assertNoQmlErrors(warnings);
    }

    // Stacked, opening a chat is a push, not a swap: the chat comes in from
    // the right edge over the list, and the list only drops out once it lands.
    // Both panes are on screen at full width for the length of that, which no
    // two panes of a split can be - hence the chat living outside it, over a
    // slot the split sizes in its place.
    void narrowLayoutPushesTheChatOverTheList() {
        constexpr int kNarrow = 400; // one column, under the 720 breakpoint
        constexpr int kWide = 1000;  // rail, list and chat side by side

        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });
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
        // The split re-lays out on the next polish; let it settle before
        // reading where the panes meet.
        QTRY_COMPARE(chat->x() + chat->width(), shell->width());
        const qreal seam = list->mapToItem(shell, QPointF(list->width(), 0)).x();
        QVERIFY2(qAbs(chat->x() - seam) <= 2,
                 qPrintable(QString("the chat starts at %1, the list runs to %2")
                                .arg(chat->x())
                                .arg(seam)));

        // Out of the split but under it, so a press on the seam still reaches
        // the divider rather than the chat's leading edge.
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
        assertNoQmlErrors(warnings);
    }

    // Every colour in the active palette has to reach the property named after
    // it, and the failure is silent: a QML property whose name starts with "on"
    // followed by a capital reads as a signal handler, so `onAccent` never took
    // its initialiser and every icon drawn on an accent fill came out
    // default-constructed black.
    void themePropertiesCarryTheirPaletteColour() {
        QQmlEngine e;
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
        QQmlEngine e;
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

    // The rail stands beside the chat list wide, and slides over it narrow, so
    // the two fills have to be different paint. They cannot be told apart at
    // all if a palette hands both the same value.
    void theRailNeverSharesAFillWithTheChatList() {
        QQmlEngine e;
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
        QStringList warnings;
        QQmlEngine e;
        QObject::connect(&e, &QQmlEngine::warnings,
                         [&](const QList<QQmlError> &ws) {
                             for (const QQmlError &w : ws)
                                 warnings << w.toString();
                         });
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

        assertNoQmlErrors(warnings);
    }

    // The badge is the row's only sign of unread mail, so it has to carry the
    // count and, once the chat is read, leave without a trace.
    void unreadChatsWearABadge() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // A room's row says what state it is in, which is the only place a failed
    // join or a room we have been dropped from is visible without opening it.
    void roomRowsAreStyledByTheirState() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        auto *theme = e.singletonInstance<QObject *>("Quack", "Theme");
        QVERIFY(theme);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // The row menu is one instance shared by every row, so which verbs it shows
    // is entirely a function of the entry it was opened for. A contact offered
    // a room's join, or a room offered a call, would both be nonsense.
    void rowMenuFollowsTheRowItWasOpenedFor() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // Starting a chat with someone not on the list yet: the chat opens either
    // way, and the roster write is what the tick decides. The JID is cut back to
    // bare lower case first, or the chat opened would be one the list can never
    // produce a row for.
    void newChatOpensTheChatAndOptionallyAddsTheContact() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // Joining a room is a bookmark write with autojoin set - that is what keeps
    // tacky rejoining it - and the typed nick and password have to ride along.
    void joinRoomWritesABookmark() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // Joining from the menu has to reach the backend as a bookmark write, and a
    // remove has to wait for the confirmation rather than fire on the click.
    void rowMenuEditsReachTheBackend() {
        QStringList warnings;
        QQmlEngine e;
        auto *app = e.singletonInstance<AppController *>("Quack", "App");
        QVERIFY(app);
        QObject::connect(&e, &QQmlEngine::warnings, [&](const QList<QQmlError> &ws) {
            for (const QQmlError &w : ws)
                warnings << w.toString();
        });

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

        assertNoQmlErrors(warnings);
    }

    // The per-account models are cached on the App singleton, so nothing else
    // would ever let go of one for an account that has been removed.
    void removingAnAccountDropsWhatWasCachedForIt() {
        QQmlEngine e;
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
