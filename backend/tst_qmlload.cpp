// Loads the app QML headless with the real registered types. Binding errors
// (ReferenceError etc.) only surface as warnings, so collect and fail on them.
#include <QtTest>
#include <QImage>
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

#include "AccountSettings.h"
#include "AppController.h"
#include "OmemoDevicesModel.h"
#include "SearchModel.h"

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
};

QTEST_MAIN(TestQmlLoad)
#include "tst_qmlload.moc"
