// Pictures and files in a bubble, at each stage a transfer can be caught in.
#include <QtTest>
#include <QImage>
#include <QQmlComponent>
#include <QTemporaryDir>

#include "AppController.h"
#include "AppSettings.h"

#include "QmlTestSupport.h"

using namespace qmltest;

class TestTransfers : public QObject {
    Q_OBJECT

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

private slots:
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

        // Avatars in the feed share the card. On until something says
        // otherwise, so the box shows what is in force before anything is
        // stored, as the dots above do.
        QQuickItem *avatarBox = option("chatAvatarsBox");
        QVERIFY(avatarBox);
        QVERIFY(app->settings()->chatAvatars());
        QVERIFY(avatarBox->property("checked").toBool());

        // A value arriving from the store, which is how a second window and a
        // fresh start both learn it.
        app->settings()->handleEvent(
            "setting", "Changed",
            QVariantMap{{"key", "chat_avatars"}, {"value", "0"}});
        QTRY_VERIFY(!avatarBox->property("checked").toBool());

        // And back the other way, through the box itself.
        const QPoint onBox = prefsWin->contentItem()
                                 ->mapFromItem(avatarBox,
                                               QPointF(avatarBox->height() / 2,
                                                       avatarBox->height() / 2))
                                 .toPoint();
        QVERIFY2(prefsWin->contentItem()->boundingRect().contains(onBox),
                 qPrintable(QString("avatar box at %1,%2 in a %3x%4 viewport")
                                .arg(onBox.x()).arg(onBox.y())
                                .arg(prefsWin->contentItem()->width())
                                .arg(prefsWin->contentItem()->height())));
        QTest::mouseClick(prefsWin, Qt::LeftButton, Qt::NoModifier, onBox);
        QTRY_VERIFY(app->settings()->chatAvatars());
        QVERIFY(avatarBox->property("checked").toBool());

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
};

QTEST_MAIN(TestTransfers)
#include "tst_qmlload_transfers.moc"
