// The account settings window, the avatar it publishes, the devices it trusts,
// and the contact page that asks the same of somebody else.
#include <QtTest>
#include <QImage>
#include <QQmlComponent>

#include "AccountSettings.h"
#include "AppController.h"
#include "AvatarEncoder.h"
#include "ChatListModel.h"
#include "OmemoDevicesModel.h"

#include "QmlTestSupport.h"

using namespace qmltest;

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
} // namespace

class TestSettings : public QObject {
    Q_OBJECT

    static QVariant device(int id, const QString &trust) {
        return QVariantMap{{"device", id},
                           {"trust", trust},
                           {"active", true},
                           {"fingerprint", QString(64, QChar('a'))}};
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

private slots:
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

QTEST_MAIN(TestSettings)
#include "tst_qmlload_settings.moc"
