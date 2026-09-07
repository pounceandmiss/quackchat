// The strip under the feed: what typing grows, what Enter does, what the
// padlock beside it changes, and what a row that failed to send offers.
#include "ChatPageTest.h"

// The timestamps handed to `message retract`, in order. Filtered: the feed is
// live, so read markers and history calls come past the same spy.
static QList<qlonglong> retracts(const QSignalSpy &spy) {
    QList<qlonglong> out;
    for (const QList<QVariant> &call : spy)
        if (call.at(1).toString() == QLatin1String("retract"))
            out << call.at(2).toMap().value("timestamp").toLongLong();
    return out;
}

class TestChatPageComposer : public ChatPageTest {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void theComposerGrowsWithTheTextUpToACeiling();
    void enterSendsAndShiftEnterOpensALine();
    void replyingFromTheComposerThreadsTheTarget();
    void ticksFollowBothHops();
    void padlockFollowsTheRowStamp();
    void composerLockIsHiddenInRooms();
    void aTouchTapOnTheLockOnlyFlipsIt();
    void resendGatingFollowsTheRow();
    void onlyAFailedEncryptionOffersThePlaintextWayOut();
    void editingPutsTheMessageBackInTheComposer();
    void editAndDeleteGatingFollowsTheRow();
    void aRoomOffersNoDelete();
    void deletingAsksBeforeItGoesOut();
    void aTombstoneDrawsAPlaceholderAndOffersNothing();
    void togglingTheComposerLockChangesWhatIsSent();
    void keysOpenFromTheComposerLock();
    // Stops the shared backend, so nothing may run after it.
    void theFailedPillCarriesTheBackendsWords();
};

void TestChatPageComposer::initTestCase() {
    startBackend();
    seed("quiet@example.com", kQuiet);
    send("replies@example.com", kOriginal);
    send("select@example.com", kSelectable);
    send("room@example.com", "who said that");
    send("edit@example.com", "teh cat");
    send("delete@example.com", "said too much");
    send("tombstone@example.com", "gone by the end of this test");

    // clear@ turns OMEMO off before its send, so it is the one chat whose rows
    // come back unstamped - every other send here is encrypted by default.
    setOmemo("clear@example.com", 0);
    send("clear@example.com", "in the open");

    QCOMPARE(stored("quiet@example.com"), kQuiet);
}

void TestChatPageComposer::cleanupTestCase() { stopBackend(); }

// The composer was one line tall however much was written into it. It follows
// the text now, up to a ceiling it scrolls past rather than grows through.
void TestChatPageComposer::theComposerGrowsWithTheTextUpToACeiling() {
    const Chat chat = open("composer@example.com");
    QVERIFY(chat.win());

    auto *bar = chat.win()->findChild<QQuickItem *>("composerBar");
    auto *field = chat.win()->findChild<QQuickItem *>("composerField");
    auto *input = chat.win()->findChild<QQuickItem *>("messageInput");
    QVERIFY(bar);
    QVERIFY(field);
    QVERIFY(input);
    QTRY_VERIFY(field->width() > 0);

    // The height alone says nothing about where the field ended up: it used to
    // grow downwards out of the bar, off the bottom of the window, leaving the
    // room it had taken showing as a gap over it. It sits 10 in from the bar's
    // top and bottom edges alike, whatever it is holding.
    const auto sitsInTheBar = [bar, field] {
        const qreal top = field->mapToItem(bar, QPointF(0, 0)).y();
        return qFuzzyCompare(top, qreal(10))
                && qFuzzyCompare(top + field->height() + 10, bar->height());
    };

    // One line at rest, and the bar is the field plus its margins.
    const qreal restField = field->height();
    const qreal restBar = bar->height();
    QVERIFY(restField > 0);
    QCOMPARE(input->property("lineCount").toInt(), 1);
    QCOMPARE(restBar, restField + 20);
    QVERIFY(sitsInTheBar());

    // A long sentence wraps, and the field is taller for it - no newline
    // typed, which is how a message grows it in practice.
    input->setProperty("text", QStringLiteral("wrap me ").repeated(6).trimmed());
    QTRY_VERIFY(input->property("lineCount").toInt() > 1);
    QTRY_VERIFY(field->height() > restField);
    QCOMPARE(bar->height(), field->height() + 20);
    QVERIFY(sitsInTheBar());

    // Line by line it keeps up, as far as the ceiling.
    QStringList lines{QStringLiteral("line 1")};
    qreal last = 0;
    for (int n = 2; n <= 6; ++n) {
        lines << QStringLiteral("line %1").arg(n);
        input->setProperty("text", lines.join(QLatin1Char('\n')));
        QTRY_COMPARE(input->property("lineCount").toInt(), lines.size());
        QTRY_VERIFY2(field->height() > last,
                     qPrintable(QStringLiteral("stuck at %1 on %2 lines")
                                        .arg(field->height())
                                        .arg(lines.size())));
        last = field->height();
        QTRY_VERIFY2(sitsInTheBar(),
                     qPrintable(QStringLiteral("field %1 tall sits at %2 in a "
                                               "bar %3 tall, on %4 lines")
                                        .arg(field->height())
                                        .arg(field->mapToItem(bar, QPointF(0, 0)).y())
                                        .arg(bar->height())
                                        .arg(lines.size())));
    }

    // Past the ceiling the field holds still and the text scrolls inside it.
    for (int n = 0; n < 30; ++n)
        lines << QStringLiteral("and more");
    input->setProperty("text", lines.join(QLatin1Char('\n')));
    QTRY_COMPARE(input->property("lineCount").toInt(), lines.size());
    settle();
    QCOMPARE(field->height(), last);
    QVERIFY(sitsInTheBar());
    QVERIFY(input->property("contentHeight").toReal() > field->height());

    // Emptying it puts the bar back where it started.
    input->setProperty("text", QString());
    QTRY_COMPARE(field->height(), restField);
    QCOMPARE(bar->height(), restBar);
    QVERIFY(sitsInTheBar());
}

// Enter still sends, as it did before the field could hold more than a line.
// Shift+Enter is what makes the second one.
void TestChatPageComposer::enterSendsAndShiftEnterOpensALine() {
    const Chat chat = open("newlines@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_VERIFY(chat.model() != nullptr);

    auto *input = chat.win()->findChild<QQuickItem *>("messageInput");
    QVERIFY(input);
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());

    type(chat.win(), "first");
    QTest::keyClick(chat.win(), Qt::Key_Return, Qt::ShiftModifier);
    type(chat.win(), "second");
    QCOMPARE(input->property("text").toString(), QStringLiteral("first\nsecond"));
    QCOMPARE(input->property("lineCount").toInt(), 2);
    QCOMPARE(chat.count(), 0);

    QTest::keyClick(chat.win(), Qt::Key_Return);
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    QCOMPARE(model->data(model->index(0), ChatModel::BodyRole).toString(),
             QStringLiteral("first\nsecond"));
    QCOMPARE(input->property("text").toString(), QString());
}

// The composer used to hold the quoted body and nothing else, so replying sent
// an ordinary message. What has to survive the trip is the target's timestamp.
void TestChatPageComposer::replyingFromTheComposerThreadsTheTarget() {
    const Chat chat = open("replies@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    const qlonglong target =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();

    QVERIFY(chat.win());
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QVERIFY(QMetaObject::invokeMethod(page, "startReply", Q_ARG(QVariant, target),
                                      Q_ARG(QVariant, kOriginal),
                                      Q_ARG(QVariant, true)));
    QVERIFY(page->property("replying").toBool());

    auto *input = chat.win()->findChild<QObject *>("messageInput");
    QVERIFY(input);
    input->setProperty("text", "my answer");
    QVERIFY(QMetaObject::invokeMethod(page, "sendCurrent"));

    QTRY_COMPARE(chat.count(), 2);
    QCOMPARE(model->data(model->index(0), ChatModel::BodyRole).toString(),
             QString("my answer"));
    QCOMPARE(model->data(model->index(0), ChatModel::ReplyBodyRole).toString(), kOriginal);

    // Sending clears the banner, so the next message is not a reply too.
    QVERIFY(!page->property("replying").toBool());

    // ...and that row's bubble draws the quote it came back with.
    QVERIFY(chat.row(0));
    QQuickItem *quote = findItem(chat.row(0), "replyQuote");
    QVERIFY(quote);
    QTRY_VERIFY(quote->isVisible());
}

// The old rule read server_status alone and called anything the server had
// "read", so an unsent message claimed the peer had read it.
void TestChatPageComposer::ticksFollowBothHops() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    struct Case { const char *server; const char *remote; const char *want; };
    const Case cases[] = {
        {"pending", "none", "pending"},
        {"uploading", "none", "pending"},
        {"failed", "none", "failed"},
        {"", "none", "sent"},
        {"", "delivered", "delivered"},
        {"", "read", "read"},
        // The far end cannot be ahead of our own server, but if it says so the
        // unsent state still wins - that is the axis the user is waiting on.
        {"pending", "read", "pending"},
    };
    for (const Case &c : cases) {
        QVariant out;
        QVERIFY(QMetaObject::invokeMethod(page, "fmtStatus", Q_RETURN_ARG(QVariant, out),
                                          Q_ARG(QVariant, QString(c.server)),
                                          Q_ARG(QVariant, QString(c.remote))));
        QCOMPARE(out.toString(), QString(c.want));
    }

    // Nothing is connected here, so these really are unsent, and the bubble
    // has to say so rather than showing a read tick.
    QVERIFY(chat.row(0));
    QQuickItem *tick = findItem(chat.row(0), "statusTick");
    QVERIFY(tick);
    // Ticks are drawn now, so the assertion is which Icons path the row picked
    // rather than which character it typed.
    QObject *icons = m_engine->singletonInstance<QObject *>("Quack", "Icons");
    QVERIFY(icons);
    QCOMPARE(tick->property("path").toString(), icons->property("schedule").toString());
}

// Reactions used to live in the view and go nowhere. They are the backend's
// set now, so the chips have to come back from it.
// A 1:1 chat encrypts by default, so an ordinary send is padlocked without the
// user doing anything; the badge has to come off only where the row really is
// in the open. Nothing is connected, so neither row got out - which is the
// point: the padlock reports how the message is carried, not how far it got.
void TestChatPageComposer::padlockFollowsTheRowStamp() {
    const Chat encrypted = open("quiet@example.com");
    QVERIFY(encrypted.feed);
    QTRY_COMPARE(encrypted.count(), kQuiet);
    QVERIFY(encrypted.row(0));
    QQuickItem *lock = findItem(encrypted.row(0), "lockBadge");
    QVERIFY(lock);
    QVERIFY(lock->isVisible());
    QCOMPARE(lock->property("text").toString(), QString("🔒"));

    const Chat clear = open("clear@example.com");
    QVERIFY(clear.feed);
    QTRY_COMPARE(clear.count(), 1);
    QVERIFY(clear.row(0));
    QQuickItem *openLock = findItem(clear.row(0), "lockBadge");
    QVERIFY(openLock);
    QVERIFY(!openLock->isVisible());
}

// A room's messages go out in the clear whatever the switch says, so it has
// nothing to offer there.
void TestChatPageComposer::composerLockIsHiddenInRooms() {
    const Chat oneToOne = open("quiet@example.com");
    QVERIFY(oneToOne.win());
    auto *lock = shownLock(oneToOne.win());
    QVERIFY(lock);
    QVERIFY(lock->property("visible").toBool());

    const Chat room = open("room@example.com", true);
    QVERIFY(room.win());
    auto *roomLock = room.win()->findChild<QQuickItem *>("omemoToggle");
    QVERIFY(roomLock);
    QVERIFY(!roomLock->property("visible").toBool());
}

// A touch point carries no button, so the padlock's right-click handler was
// offered every tap: on a phone the keys came up over the switch it flipped.
void TestChatPageComposer::aTouchTapOnTheLockOnlyFlipsIt() {
    const Chat chat = open("touch@example.com");
    QVERIFY(chat.win());
    auto *lock = shownLock(chat.win());
    QVERIFY(lock);
    auto *menu = lock->findChild<QObject *>("lockMenu");
    QVERIFY(menu);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QTRY_VERIFY(page->property("encryptOn").toBool());

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint centre =
        lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, centre);
    QTest::touchEvent(chat.win(), finger).release(0, centre);

    QTRY_VERIFY(!page->property("encryptOn").toBool());
    settle();
    QVERIFY2(!menu->property("opened").toBool(),
             "a tap on the padlock brought up the keys menu");
}

void TestChatPageComposer::resendGatingFollowsTheRow() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    struct Case {
        bool outgoing; const char *status; const char *enc; const char *why;
        bool retry; bool plain;
    };
    const Case cases[] = {
        // The encryption refused: both ways out, and the plaintext one is the
        // only one that can actually work.
        {true, "failed", "omemo", "encrypt", true, true},
        // The message got out of the encryption and fell over after; sending it
        // in the clear would give up privacy for nothing.
        {true, "failed", "omemo", "delivery", true, false},
        {true, "failed", "", "delivery", true, false},
        // Still on its way, or already gone: nothing to send again.
        {true, "pending", "omemo", "", false, false},
        {true, "sent", "omemo", "", false, false},
        // A stale reason left by an earlier failure, on a row that is now fine.
        {true, "sent", "omemo", "encrypt", false, false},
        // Not ours to send.
        {false, "failed", "omemo", "encrypt", false, false},
    };
    for (const Case &c : cases) {
        QVariant retry, plain;
        QVERIFY(QMetaObject::invokeMethod(page, "canRetry", Q_RETURN_ARG(QVariant, retry),
                                          Q_ARG(QVariant, c.outgoing),
                                          Q_ARG(QVariant, QString(c.status))));
        QVERIFY(QMetaObject::invokeMethod(page, "canResendPlain", Q_RETURN_ARG(QVariant, plain),
                                          Q_ARG(QVariant, c.outgoing),
                                          Q_ARG(QVariant, QString(c.status)),
                                          Q_ARG(QVariant, QString(c.enc)),
                                          Q_ARG(QVariant, QString(c.why))));
        QCOMPARE(retry.toBool(), c.retry);
        QCOMPARE(plain.toBool(), c.plain);
    }
}

// The entries are drawn from those folds, and an entry that is not offered must
// leave no gap where it would have been.
void TestChatPageComposer::onlyAFailedEncryptionOffersThePlaintextWayOut() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);

    auto *retry = menu->findChild<QQuickItem *>("retryEntry");
    auto *plain = menu->findChild<QQuickItem *>("resendPlainEntry");
    QVERIFY(retry);
    QVERIFY(plain);
    // An entry that is not on offer takes up none of the menu, whether or not
    // the menu is open - the view only draws what has a height.
    QCOMPARE(retry->height(), 0.0);
    QCOMPARE(plain->height(), 0.0);

    const qlonglong ts =
        chat.model()->data(chat.model()->index(0), ChatModel::TimestampRole).toLongLong();
    chat.model()->applyFields(ts, QVariantMap{{"server_status", "failed"},
                                              {"fail_reason", "encrypt"}});
    QTRY_VERIFY(plain->height() > 0.0);
    QVERIFY(retry->height() > 0.0);

    // The encryption was never the problem for a delivery failure, so sending
    // it in the clear would give up privacy for nothing.
    chat.model()->applyFields(ts, QVariantMap{{"fail_reason", "delivery"}});
    QTRY_COMPARE(plain->height(), 0.0);
    QVERIFY(retry->height() > 0.0);
}

// An edit fills the field the draft was in, so the banner has to say which of
// the two the composer is doing, and the draft has to survive the round trip.
void TestChatPageComposer::editingPutsTheMessageBackInTheComposer() {
    const Chat chat = open("edit@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    auto *banner = chat.win()->findChild<QQuickItem *>("composerBanner");
    auto *title = chat.win()->findChild<QQuickItem *>("bannerTitle");
    auto *input = chat.win()->findChild<QObject *>("messageInput");
    QVERIFY(page);
    QVERIFY(banner);
    QVERIFY(title);
    QVERIFY(input);
    // Collapsed, not merely invisible: the strip is one of the things the feed
    // measures itself against.
    QTRY_COMPARE(banner->height(), 0.0);

    input->setProperty("text", "half a sentence");

    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();
    QVERIFY(QMetaObject::invokeMethod(page, "startEdit", Q_ARG(QVariant, QVariant(ts)),
                                      Q_ARG(QVariant, QString("teh cat"))));
    QVERIFY(page->property("editing").toBool());
    QVERIFY(!page->property("replying").toBool());
    // Open to the whole strip, not a sliver of one.
    QTRY_VERIFY(banner->height() > 40.0);
    QCOMPARE(title->property("text").toString(), QString("Editing message"));
    // The field is seeded from the draft rather than bound to it, so this is
    // the assertion that the page caught it up.
    QCOMPARE(input->property("text").toString(), QString("teh cat"));

    QVERIFY(QMetaObject::invokeMethod(page, "cancelEdit"));
    QVERIFY(!page->property("editing").toBool());
    // Shut rather than exactly zero: the strip animates its layout height, and
    // the last pass can leave a fraction of a pixel behind with nothing left to
    // trigger another. 52 against under a pixel is the difference that matters.
    QTRY_VERIFY(banner->height() < 1.0);
    QCOMPARE(input->property("text").toString(), QString("half a sentence"));
}

void TestChatPageComposer::editAndDeleteGatingFollowsTheRow() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    struct Case { bool outgoing; bool retracted; bool edit; bool del; };
    const Case cases[] = {
        {true, false, true, true},
        // Not ours to correct or withdraw.
        {false, false, false, false},
        // A retraction is sticky: tacky will not let an edit past it, and there
        // is nothing left to withdraw twice.
        {true, true, false, false},
        {false, true, false, false},
    };
    for (const Case &c : cases) {
        QVariant edit, del;
        QVERIFY(QMetaObject::invokeMethod(page, "canEditMessage",
                                          Q_RETURN_ARG(QVariant, edit),
                                          Q_ARG(QVariant, c.outgoing),
                                          Q_ARG(QVariant, c.retracted)));
        QVERIFY(QMetaObject::invokeMethod(page, "canDeleteMessage",
                                          Q_RETURN_ARG(QVariant, del),
                                          Q_ARG(QVariant, c.outgoing),
                                          Q_ARG(QVariant, c.retracted)));
        QCOMPARE(edit.toBool(), c.edit);
        QCOMPARE(del.toBool(), c.del);
    }

    // And the entries drawn from those folds, on a row that is our own.
    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *editEntry = menu->findChild<QQuickItem *>("editEntry");
    auto *deleteEntry = menu->findChild<QQuickItem *>("deleteEntry");
    QVERIFY(editEntry);
    QVERIFY(deleteEntry);
    QTRY_VERIFY(editEntry->height() > 0.0);
    QVERIFY(deleteEntry->height() > 0.0);
}

// Withdrawing your own message is the 1:1 path. A room's is moderation, which
// asks the service to retract anyone's message and is not offered here, so tacky
// would ignore a retract sent for a room - there is nothing to show.
void TestChatPageComposer::aRoomOffersNoDelete() {
    const Chat chat = open("room@example.com", true);
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *editEntry = menu->findChild<QQuickItem *>("editEntry");
    auto *deleteEntry = menu->findChild<QQuickItem *>("deleteEntry");
    QVERIFY(editEntry);
    QVERIFY(deleteEntry);
    // Correcting still works in a room; only the deletion has no path.
    QTRY_VERIFY(editEntry->height() > 0.0);
    QCOMPARE(deleteEntry->height(), 0.0);
}

// The one action here that reaches everyone who already has the message, and
// cannot be taken back.
void TestChatPageComposer::deletingAsksBeforeItGoesOut() {
    const Chat chat = open("delete@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    const qlonglong ts =
        chat.model()->data(chat.model()->index(0), ChatModel::TimestampRole).toLongLong();

    QSignalSpy sent(m_app->backend(), &TackyBackend::sent);
    QVERIFY(QMetaObject::invokeMethod(page, "confirmDelete",
                                      Q_ARG(QVariant, QVariant(ts))));
    auto *confirm = chat.win()->findChild<QObject *>("deleteConfirm");
    QVERIFY(confirm);
    QTRY_VERIFY(confirm->property("visible").toBool());
    // Asking is not doing: nothing has gone out yet.
    QCOMPARE(retracts(sent), QList<qlonglong>{});

    QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
    QTRY_COMPARE(retracts(sent), QList<qlonglong>{ts});
}

// The row is kept so paging and replies still resolve, but everything it used
// to carry is gone - and the reactions and padlock still on it are not drawn.
void TestChatPageComposer::aTombstoneDrawsAPlaceholderAndOffersNothing() {
    const Chat chat = open("tombstone@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    QTRY_VERIFY(body->property("visible").toBool());

    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();
    // Stamped and reacted to first, so what the tombstone stops drawing was
    // demonstrably being drawn a moment earlier.
    model->applyFields(ts, QVariantMap{
        {"encryption", "omemo"},
        {"reactions", QVariantMap{{"👍", QVariantMap{{"reactors", QVariantList{"b@h"}},
                                                     {"mine", false}}}}}});
    auto *tick = findItem(chat.feed, "statusTick");
    auto *lock = findItem(chat.feed, "lockBadge");
    auto *chips = findItem(chat.feed, "reactionRow");
    QVERIFY(tick);
    QVERIFY(lock);
    QVERIFY(chips);
    QTRY_VERIFY(lock->property("visible").toBool());
    QVERIFY(tick->property("visible").toBool());
    QVERIFY(chips->property("visible").toBool());

    model->applyRetracted(ts);

    QQuickItem *stone = findItem(chat.feed, "tombstone");
    QVERIFY(stone);
    QTRY_VERIFY(stone->property("visible").toBool());
    // Something is actually drawn in the row's place, not just a flag flipped.
    QVERIFY(stone->height() > 0.0);
    QVERIFY(!body->property("visible").toBool());
    // All three are still on the row - only the content was dropped - and none
    // of them is drawn.
    QVERIFY(!tick->property("visible").toBool());
    QVERIFY(!lock->property("visible").toBool());
    QVERIFY(!chips->property("visible").toBool());
    QVERIFY(!model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty());

    // There is no body to reply to, copy, or pick words out of. View XML stays:
    // a row with nothing recorded is itself an answer the viewer is there for.
    for (const char *gone : {"replyEntry", "copyEntry", "selectEntry",
                             "editEntry", "deleteEntry"}) {
        auto *entry = menu->findChild<QQuickItem *>(gone);
        QVERIFY2(entry, gone);
        QTRY_VERIFY2(entry->height() == 0.0, gone);
    }
    auto *xml = menu->findChild<QQuickItem *>("viewXmlEntry");
    QVERIFY(xml);
    QVERIFY(xml->height() > 0.0);
}

// The whole path in one go: the padlock in the composer, through tacky's stored
// per-chat setting, to how the next message is actually stamped - and only the
// next one, since the messages already sent keep the terms they went out under.
void TestChatPageComposer::togglingTheComposerLockChangesWhatIsSent() {
    const Chat chat = open("toggle@example.com");
    QVERIFY(chat.feed);
    auto *lock = chat.win()->findChild<QQuickItem *>("omemoToggle");
    QVERIFY(lock);

    chat.model()->send("under the lock");
    QTRY_COMPARE(chat.count(), 1);
    QCOMPARE(chat.model()->data(chat.model()->index(0), ChatModel::EncryptionRole).toString(),
             QString("omemo"));

    const QPointF centre = lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2));
    QTest::mouseClick(chat.win(), Qt::LeftButton, Qt::NoModifier, centre.toPoint());

    chat.model()->send("in the open");
    QTRY_COMPARE(chat.count(), 2);
    QCOMPARE(chat.model()->data(chat.model()->index(0), ChatModel::EncryptionRole).toString(),
             QString());
    QCOMPARE(chat.model()->data(chat.model()->index(1), ChatModel::EncryptionRole).toString(),
             QString("omemo"));
}

// The contact's page hangs off the padlock too, since that is the control that
// says whether their keys are being used. A room has neither.
void TestChatPageComposer::keysOpenFromTheComposerLock() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.win());
    auto *lock = shownLock(chat.win());
    QVERIFY(lock);
    auto *menu = lock->findChild<QObject *>("lockMenu");
    QVERIFY(menu);
    QVERIFY(!menu->property("opened").toBool());

    const QPointF centre = lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2));
    QTest::mouseClick(chat.win(), Qt::RightButton, {}, centre.toPoint());
    QTRY_VERIFY(menu->property("opened").toBool());
    QVERIFY(lock->findChild<QQuickItem *>("keysEntry"));

    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QVariant window;
    QVERIFY(QMetaObject::invokeMethod(page, "openContact", Q_RETURN_ARG(QVariant,window)));
    auto *keys = qobject_cast<QQuickWindow *>(window.value<QObject *>());
    QVERIFY(keys); // a window on desktop; the mobile branch opens a sheet
    QCOMPARE(keys->property("jid").toString(), QString("quiet@example.com"));
    QVERIFY(QMetaObject::invokeMethod(keys, "close"));
    QCoreApplication::processEvents();

    // Nothing to show for a room, and nothing to open.
    const Chat room = open("room@example.com", true);
    auto *roomPage = room.win()->findChild<QObject *>("chatPane");
    QVERIFY(roomPage);
    QVariant none;
    QVERIFY(QMetaObject::invokeMethod(roomPage, "openContact", Q_RETURN_ARG(QVariant,none)));
    QVERIFY(!none.value<QObject *>());
}

// Must stay last: stopping the backend is what fails the `before` page that
// is out, and nothing after it would have one.
void TestChatPageComposer::theFailedPillCarriesTheBackendsWords() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_VERIFY(chat.model()->loadingOlder());

    m_app->backend()->stop();
    QTRY_VERIFY(!chat.model()->loadError().isEmpty());

    QQuickItem *label = chat.feed->findChild<QQuickItem *>("olderPillLabel");
    QQuickItem *retry = chat.feed->findChild<QQuickItem *>("olderPillRetry");
    QVERIFY(label);
    QVERIFY(retry);
    QTRY_COMPARE(label->property("text").toString(), chat.model()->loadError());
    QVERIFY(retry->isVisible());
    QVERIFY(label->width() <= chat.feed->width() - 96); // elided, not widened
}

QTEST_MAIN(TestChatPageComposer)
#include "tst_chatpage_composer.moc"
