// Picking words and messages out of the feed: what a drag selects, what a press
// selects, and the menus and reaction chips either one leaves behind.
#include "ChatPageTest.h"

const QString kStyled = QStringLiteral("*bold* and plain");

class TestChatPageSelection : public ChatPageTest {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void bubbleRendersMarkupAsRichText();
    void bubbleGoesBackToPlainWhenAMarkLeaves();
    void mouseDragSelectsBodyText();
    void onlyOneMessageKeepsItsPickedWords();
    void theBubbleMenuTakesWhatTheDragPickedOut();
    void clickingTheWordsPicksTheMessageWhileSelecting();
    void wordsStayDeafUntilTheirMessageIsPicked();
    void pickedWordsAreLetGoWithTheirMessage();
    void rightClickStillOpensTheBubbleMenu();
    void aTouchTapOpensTheBubbleMenu();
    void aPressOutsideAMenuOnlyDismissesIt();
    void aLongPressSelectsAndOffersItsActions();
    void theSelectionBarActsOnTheOneMessage();
    void takingAReactionDropsThePressedSelection();
    void pressingASelectedMessageAgainHandsOverItsWords();
    void tappingAnotherMessageAddsItToTheSelection();
    void tappingAReactionChipDoesNotAlsoOpenTheMenu();
    void theBubbleMenuLeavesTheComposerFocused();
    void reactionChipsShowTheBackendSet();
    void hoveringAReactionGrowsItsGlyphRatherThanTheItem();
    void aReactionThatArrivesPopsItsChip();
    void chipsTheRowWasBuiltWithDoNotPop();
};

void TestChatPageSelection::initTestCase() {
    startBackend();
    seed("friend@example.com", kSeeded);
    send("styled@example.com", kStyled);
    send("select@example.com", kSelectable);
    // Two of them, so a selection can grow past the one a press started it on.
    send("pair@example.com", "the older one");
    send("pair@example.com", "the newer one");
    send("react@example.com", "react to me");
    send("chip@example.com", "already reacted to");
    send("pop@example.com", "watch the chip land");
    send("drawn@example.com", "reacted to earlier");

    QCOMPARE(stored("friend@example.com"), kSeeded);
}

void TestChatPageSelection::cleanupTestCase() { stopBackend(); }

// The model builds the markup; this is the other half - that Qt parses it into
// styled runs instead of drawing the tags.
void TestChatPageSelection::bubbleRendersMarkupAsRichText() {
    const Chat chat = open("styled@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    // Only reached when the model handed up markup; a bare body stays plain.
    QCOMPARE(body->property("textFormat").toInt(), int(Qt::RichText));

    // Qt re-serialises the document it parsed, so a bold weight in there is
    // proof the <b> became a style rather than four literal characters.
    const QString doc = body->property("text").toString();
    QVERIFY2(doc.contains(QLatin1String("font-weight:700")), qPrintable(doc));

    QString plain;
    QVERIFY(QMetaObject::invokeMethod(body, "getText", Q_RETURN_ARG(QString, plain),
                                      Q_ARG(int, 0), Q_ARG(int, 14)));
    QCOMPARE(plain, QString("bold and plain"));
}

// A search mark is markup over a body that has none of its own, so a marked
// row turns rich and falls back to plain once the mark moves on. Going back is
// the direction that breaks: Qt fills the plain document with the rich one
// serialised, and the reader gets the HTML instead of the message.
void TestChatPageSelection::bubbleGoesBackToPlainWhenAMarkLeaves() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    ChatModel *model = chat.model();
    QVERIFY(model);
    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QCOMPARE(body->property("textFormat").toInt(), int(Qt::PlainText));

    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();
    model->highlightMatches(ts, {QVariantMap{{"offset", 4}, {"length", 5}}});
    QTRY_COMPARE(body->property("textFormat").toInt(), int(Qt::RichText));

    model->highlightMatches(0, {});
    QTRY_COMPARE(body->property("textFormat").toInt(), int(Qt::PlainText));

    QString plain;
    QVERIFY(QMetaObject::invokeMethod(body, "getText", Q_RETURN_ARG(QString, plain),
                                      Q_ARG(int, 0), Q_ARG(int, kSelectable.size())));
    QCOMPARE(plain, kSelectable);
}

// Whether the body or the swipe handler wins the mouse drag is decided by
// event delivery, so nothing short of a synthesised drag settles it.
void TestChatPageSelection::mouseDragSelectsBodyText() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QVERIFY(body->width() > 0 && body->height() > 0);

    const qreal parked = chat.prop("contentY");
    const QString picked = dragThroughWords(chat.win(), body);
    QVERIFY2(picked.size() > 3, qPrintable(QString("selected only %1").arg(picked)));
    QVERIFY2(kSelectable.contains(picked), qPrintable(picked));

    // A horizontal drag must not have flicked the feed out from under it.
    QCOMPARE(chat.prop("contentY"), parked);
}

// The highlight is persistent so it survives the focus going to the composer,
// which also means nothing clears the one left on the message before it.
void TestChatPageSelection::onlyOneMessageKeepsItsPickedWords() {
    const Chat chat = open("pair@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 2);

    QQuickItem *newest = findItem(chat.row(0), "bubbleText");
    QQuickItem *older = findItem(chat.row(1), "bubbleText");
    QVERIFY(newest);
    QVERIFY(older);

    QVERIFY(!dragThroughWords(chat.win(), older).isEmpty());
    QVERIFY(!dragThroughWords(chat.win(), newest).isEmpty());
    QTRY_COMPARE(older->property("selectedText").toString(), QString());
}

// The menu over a message answers for the whole of it, which leaves the words
// a drag picked out with no way off the screen but Ctrl+C.
void TestChatPageSelection::theBubbleMenuTakesWhatTheDragPickedOut() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *words = menu->findChild<QQuickItem *>("copySelectionEntry");
    auto *whole = menu->findChild<QQuickItem *>("copyEntry");
    QVERIFY(words);
    QVERIFY(whole);

    // Nothing picked out: only the message can be copied, under its plain name.
    QVERIFY(!words->property("offered").toBool());
    QCOMPARE(whole->property("text").toString(), QString("Copy"));

    const QString picked = dragThroughWords(chat.win(), body);
    QVERIFY(picked.size() > 3);

    // The right button opens the menu without disturbing the selection.
    QTest::mouseClick(chat.win(), Qt::RightButton, {},
                      body->mapToScene(QPointF(body->width() / 2, body->height() / 2))
                          .toPoint());
    QTRY_VERIFY(menu->property("opened").toBool());
    QCOMPARE(body->property("selectedText").toString(), picked);
    QVERIFY(words->property("offered").toBool());
    QCOMPARE(whole->property("text").toString(), QString("Copy message"));

    // The words as the drag left them, not the body they came from.
    QSignalSpy took(menu->parent(), SIGNAL(copyTextRequested(QString)));
    QVERIFY(QMetaObject::invokeMethod(words, "triggered"));
    QCOMPARE(took.count(), 1);
    QCOMPARE(took.at(0).at(0).toString(), picked);
}

// A message is picked by clicking it, and its words are most of what there is
// to click - but the body takes a mouse press for itself.
void TestChatPageSelection::clickingTheWordsPicksTheMessageWhileSelecting() {
    const Chat chat = open("pair@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 2);

    QQuickItem *newest = findItem(chat.row(0), "bubbleText");
    QQuickItem *older = findItem(chat.row(1), "bubbleText");
    QVERIFY(newest);
    QVERIFY(older);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint held =
        newest->mapToScene(QPointF(newest->width() / 2, newest->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, held);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, held);

    const QPoint onWords =
        older->mapToScene(QPointF(older->width() / 2, older->height() / 2)).toPoint();
    QTest::mouseClick(chat.win(), Qt::LeftButton, {}, onWords);
    QTRY_COMPARE(page->property("selectedCount").toInt(), 2);

    // And the same click unpicks it.
    QTest::mouseClick(chat.win(), Qt::LeftButton, {}, onWords);
    QTRY_COMPARE(page->property("selectedCount").toInt(), 1);
}

// Android takes a long press on a live body for its own text selection, so a
// body live from the start would answer the very press that picks the
// message. It comes alive once the message is picked, and the press after
// that is Android's. A synthesised press never reaches Android's detector, so
// what this pins is the gate rather than the selection behind it.
void TestChatPageSelection::wordsStayDeafUntilTheirMessageIsPicked() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *bubble = qobject_cast<QQuickItem *>(menu->parent());
    QVERIFY(bubble);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    // Stand in for the platform that has a selection of its own.
    bubble->setProperty("nativeWords", true);
    QTRY_VERIFY(!body->isEnabled());

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    // Picked, so the words are the next press's to answer.
    QTRY_VERIFY(body->isEnabled());

    // A tap still reaches past the live body to tick the message off, which
    // is what watching instead of grabbing keeps.
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QTRY_COMPARE(page->property("selectedCount").toInt(), 0);
    QTRY_VERIFY(!body->isEnabled());
}

// Letting go of a message lets go of the words picked out of it, and in that
// order: a body turned deaf with a selection still on it keeps Android's
// handles behind.
void TestChatPageSelection::pickedWordsAreLetGoWithTheirMessage() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *bubble = qobject_cast<QQuickItem *>(menu->parent());
    QVERIFY(bubble);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    bubble->setProperty("nativeWords", true);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QTRY_VERIFY(body->isEnabled());

    // Standing in for the press Android would have answered itself.
    QVERIFY(QMetaObject::invokeMethod(body, "select", Q_ARG(int, 0), Q_ARG(int, 9)));
    QVERIFY(!body->property("selectedText").toString().isEmpty());

    QVERIFY(QMetaObject::invokeMethod(page, "clearSelection"));
    QTRY_COMPARE(body->property("selectedText").toString(), QString());
    QTRY_VERIFY(!body->isEnabled());
}

// The body is painted over the bubble's right-button MouseArea and has a
// context menu of its own, either of which could swallow the press.
void TestChatPageSelection::rightClickStillOpensTheBubbleMenu() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);

    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    QVERIFY(!menu->property("opened").toBool());

    QTest::mouseClick(chat.win(), Qt::RightButton, {},
                      body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint());
    QTRY_VERIFY(menu->property("opened").toBool());
}

// The touch half of the same question. The long press now starts a selection,
// so the menu moved to the tap - one that starts on the body used to stop at
// the TextEdit, which has its own handler for exactly that reason.
void TestChatPageSelection::aTouchTapOpensTheBubbleMenu() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    QVERIFY(!menu->property("opened").toBool());

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QTRY_VERIFY(menu->property("opened").toBool());

    // The reactions ride with it rather than sitting a level down inside it.
    QObject *bar = popupIn(body, "reactionBar");
    QVERIFY(bar);
    QTRY_VERIFY(bar->property("opened").toBool());

    // A tap picks nothing out: that is what the press is for.
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QCOMPARE(page->property("selectedCount").toInt(), 0);
}

// The press that dismisses a menu is spent on dismissing it: closing one
// message's menu by pressing another message used to open that one's menu on
// the way past.
void TestChatPageSelection::aPressOutsideAMenuOnlyDismissesIt() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_VERIFY(chat.count() > 3);

    QQuickItem *body = findItem(chat.row(0), "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QTRY_VERIFY(menu->property("opened").toBool());

    // Qt slides a menu that does not fit back into the window, which can leave
    // it over the rows around the one it belongs to. Those are no test of a
    // press landing outside it, so find a row that genuinely is - and fail
    // rather than pass vacuously if every visible one is covered.
    auto *anchor = menu->property("parent").value<QQuickItem *>();
    QVERIFY(anchor);
    const QRectF covered = anchor->mapRectToScene(
        QRectF(menu->property("x").toReal(), menu->property("y").toReal(),
               menu->property("width").toReal(), menu->property("height").toReal()));
    QQuickItem *clear = nullptr;
    QPoint outside;
    for (int i = 1; i < chat.count() && !clear; ++i) {
        QQuickItem *row = chat.row(i);
        QQuickItem *other = row ? findItem(row, "bubbleText") : nullptr;
        if (!other || other->width() <= 0)
            continue;
        const QPointF at =
            other->mapToScene(QPointF(other->width() / 2, other->height() / 2));
        if (!covered.contains(at)) {
            clear = other;
            outside = at.toPoint();
        }
    }
    QVERIFY2(clear, "the menu covered every other row on screen");
    QObject *otherMenu = popupIn(clear, "bubbleMenu");
    QVERIFY(otherMenu);
    QVERIFY(otherMenu != menu);

    QTest::touchEvent(chat.win(), finger).press(0, outside);
    QTest::touchEvent(chat.win(), finger).release(0, outside);

    QTRY_VERIFY(!menu->property("opened").toBool());
    // Nothing else may have come of it: no second menu, and no selection.
    QVERIFY(!otherMenu->property("opened").toBool());
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QCOMPARE(page->property("selectedCount").toInt(), 0);
}

// The press does one thing: the message joins the selection. What it can do is
// drawn along the header, and the menu, reactions included, is what a tap is
// for - so nothing comes up over the message itself.
void TestChatPageSelection::aLongPressSelectsAndOffersItsActions() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *bar = popupIn(body, "reactionBar");
    QVERIFY(bar);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();

    // Held, not tapped: it all arrives before the finger lifts.
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QVERIFY(page->property("selectionMode").toBool());

    // Neither half of what a tap brings up came with it.
    QVERIFY(!menu->property("opened").toBool());
    QVERIFY(!bar->property("opened").toBool());

    // The header now carries what the menu offers, on the one message picked.
    auto *reply = chat.win()->findChild<QQuickItem *>("selectionReply");
    QVERIFY(reply);
    QTRY_VERIFY(reply->isVisible());
    QVERIFY(chat.win()->findChild<QQuickItem *>("selectionViewXml")->isVisible());
    QVERIFY(chat.win()->findChild<QQuickItem *>("selectionCopy")->isVisible());
    // Nothing failed here, so the two that answer a failure stay away.
    QVERIFY(!chat.win()->findChild<QQuickItem *>("selectionRetry")->isVisible());
    QVERIFY(!chat.win()->findChild<QQuickItem *>("selectionResendPlain")->isVisible());

    // A held finger must not have counted as a reply swipe.
    QVERIFY(!page->property("replying").toBool());
}

// The header acts on the message the press picked out, and is done with it
// afterwards - a selection nobody asked to keep is one to put away.
void TestChatPageSelection::theSelectionBarActsOnTheOneMessage() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    auto *reply = chat.win()->findChild<QQuickItem *>("selectionReply");
    QVERIFY(reply);
    QTRY_VERIFY(reply->isVisible());
    QVERIFY(QMetaObject::invokeMethod(reply, "clicked"));

    QTRY_VERIFY(page->property("replying").toBool());
    // The quoted body is the row's, not whatever the composer had.
    QCOMPARE(page->property("replyBody").toString(), kSelectable);
    QCOMPARE(page->property("selectedCount").toInt(), 0);
}

// Picking a reaction is an answer to the message rather than an interest in
// selecting it, so a selection the press made is dropped along the way. The
// reactions ride with the menu, which the mouse opens over a selection too.
void TestChatPageSelection::takingAReactionDropsThePressedSelection() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *bar = popupIn(body, "reactionBar");
    QVERIFY(bar);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    QTest::mouseClick(chat.win(), Qt::RightButton, {}, on);
    QTRY_VERIFY(bar->property("opened").toBool());
    QCOMPARE(page->property("selectedCount").toInt(), 1);

    // Tapped where it is drawn: the bar floats in the window's overlay, so its
    // choices are somewhere a finger can actually reach.
    auto *content = bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QQuickItem *choice = findItem(content, "reactionChoice");
    QVERIFY(choice);
    QTRY_VERIFY(choice->width() > 0 && choice->height() > 0);
    const QPoint at =
        choice->mapToScene(QPointF(choice->width() / 2, choice->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, at);
    QTest::touchEvent(chat.win(), finger).release(0, at);

    QTRY_COMPARE(page->property("selectedCount").toInt(), 0);
    QTRY_VERIFY(!bar->property("opened").toBool());
}

// Pressed again once it is selected, the message gives up its words - which
// means standing its own handlers down so the TextEdit under them gets the
// press it has always been kept away from.
void TestChatPageSelection::pressingASelectedMessageAgainHandsOverItsWords() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("textSelectTs").toDouble() != 0.0, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    // The whole body to start with, for the drag to narrow down.
    QCOMPARE(body->property("selectedText").toString(), kSelectable);
    QObject *tools = popupIn(body, "textTools");
    QVERIFY(tools);
    QTRY_VERIFY(tools->property("opened").toBool());

    // Still one selected message underneath: the words are the layer above it.
    QCOMPARE(page->property("selectedCount").toInt(), 1);
}

// What the press starts is meant to be built on, so the taps after it add to
// the selection rather than opening menus over it.
void TestChatPageSelection::tappingAnotherMessageAddsItToTheSelection() {
    const Chat chat = open("pair@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 2);

    QQuickItem *newest = chat.row(0);
    QQuickItem *older = chat.row(1);
    QVERIFY(newest);
    QVERIFY(older);
    QQuickItem *newestBody = findItem(newest, "bubbleText");
    QQuickItem *olderBody = findItem(older, "bubbleText");
    QVERIFY(newestBody);
    QVERIFY(olderBody);
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint first =
        newestBody->mapToScene(QPointF(newestBody->width() / 2, newestBody->height() / 2))
            .toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, first);
    QTRY_VERIFY_WITH_TIMEOUT(page->property("selectedCount").toInt() == 1, 3000);
    QTest::touchEvent(chat.win(), finger).release(0, first);

    const QPoint second =
        olderBody->mapToScene(QPointF(olderBody->width() / 2, olderBody->height() / 2))
            .toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, second);
    QTest::touchEvent(chat.win(), finger).release(0, second);

    QTRY_COMPARE(page->property("selectedCount").toInt(), 2);
    // The tap added a message; it did not open the other bubble's menu.
    QObject *olderMenu = popupIn(olderBody, "bubbleMenu");
    QVERIFY(olderMenu);
    QVERIFY(!olderMenu->property("opened").toBool());
}

// A tap on a chip has two claims on it: the chip's own, and the row's menu
// underneath. A handler that only watched for the drag threshold would take a
// passive grab and answer alongside the chip rather than losing to it.
void TestChatPageSelection::tappingAReactionChipDoesNotAlsoOpenTheMenu() {
    const Chat chat = open("chip@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();

    model->react(ts, "\xf0\x9f\x91\x8d");
    QTRY_VERIFY_WITH_TIMEOUT(
        !model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);

    QQuickItem *row = findItem(chat.row(0), "reactionRow");
    QVERIFY(row);
    QTRY_VERIFY(row->isVisible());
    QQuickItem *chip = nullptr;
    const auto kids = row->childItems();
    for (QQuickItem *kid : kids)
        if (kid->width() > 0 && kid->height() > 0)
            chip = kid;
    QVERIFY(chip);

    QObject *menu = popupIn(row, "bubbleMenu");
    QVERIFY(menu);

    // Near the chip's top edge: the row hangs half of it past the bubble, and
    // only the half still inside is somewhere the bubble's own handler would
    // have answered.
    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on = chip->mapToScene(QPointF(chip->width() / 2, 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTest::touchEvent(chat.win(), finger).release(0, on);

    // The chip's own answer: react is a toggle, so this takes the emoji back.
    QTRY_VERIFY_WITH_TIMEOUT(
        model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);
    QVERIFY(!menu->property("opened").toBool());
}

// Android ties the software keyboard to whoever holds focus, so a menu that
// takes focus for itself drops the keyboard the moment it opens - mid-sentence,
// with the draft still in the composer.
void TestChatPageSelection::theBubbleMenuLeavesTheComposerFocused() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    auto *input = chat.win()->findChild<QQuickItem *>("messageInput");
    QVERIFY(input);
    input->forceActiveFocus();
    QVERIFY(input->hasActiveFocus());

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint on =
        body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, on);
    QTest::touchEvent(chat.win(), finger).release(0, on);
    QTRY_VERIFY(menu->property("opened").toBool());

    QVERIFY(input->hasActiveFocus());
}

void TestChatPageSelection::reactionChipsShowTheBackendSet() {
    const Chat chat = open("react@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();

    QQuickItem *row = findItem(chat.row(0), "reactionRow");
    QVERIFY(row);
    QVERIFY(!row->isVisible());

    model->react(ts, "👍");
    QTRY_VERIFY_WITH_TIMEOUT(
        !model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);
    const QVariantMap mine = model->data(model->index(0), ChatModel::ReactionsRole).toMap();
    QVERIFY(mine.value("👍").toMap().value("mine").toBool());
    QTRY_VERIFY(row->isVisible());

    // XEP-0444 react is a toggle, so the same emoji again takes it back and
    // the chip goes with it.
    model->react(ts, "👍");
    QTRY_VERIFY_WITH_TIMEOUT(
        model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);
    QTRY_VERIFY(!row->isVisible());
}

// The hover pop was an item scale, which magnifies the raster the glyph cache
// holds at the resting size - emoji are bitmaps, so it came out blocky.
void TestChatPageSelection::hoveringAReactionGrowsItsGlyphRatherThanTheItem() {
    const Chat chat = open("react@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *row = chat.row(0);
    QVERIFY(row);
    auto *bar = row->findChild<QObject *>("reactionBar");
    QVERIFY(bar);
    QVERIFY(QMetaObject::invokeMethod(bar, "open"));
    QTRY_VERIFY(bar->property("opened").toBool());

    // The bar floats in the window overlay, so its choices hang off the popup's
    // contentItem rather than the row.
    auto *content = bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QQuickItem *glyph = findItem(content, "reactionChoice");
    QVERIFY(glyph);
    const int resting = glyph->property("font").value<QFont>().pixelSize();
    QVERIFY(resting > 0);

    const QPointF centre =
        glyph->mapToScene(QPointF(glyph->width() / 2, glyph->height() / 2));
    QTest::mouseMove(chat.win(), centre.toPoint());
    QTRY_VERIFY(glyph->property("font").value<QFont>().pixelSize() > resting);
    QCOMPARE(glyph->scale(), 1.0);

    QVERIFY(QMetaObject::invokeMethod(bar, "close"));
}

void TestChatPageSelection::aReactionThatArrivesPopsItsChip() {
    const Chat chat = open("pop@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();

    QQuickItem *row = findItem(chat.row(0), "reactionRow");
    QVERIFY(row);
    QVERIFY(!findItem(row, "reactionChip"));

    model->react(ts, "\xf0\x9f\x91\x8d");

    // The reaction is what builds the chip, so there is nothing to watch from
    // beforehand: catch it mid-pop instead.
    QQuickItem *chip = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((chip = findItem(row, "reactionChip"))
                                 && chip->scale() < 1.0, 5000);
    QTRY_COMPARE(chip->scale(), 1.0);

    // React is a toggle, so this hands the fixture back clean.
    model->react(ts, "\xf0\x9f\x91\x8d");
    QTRY_VERIFY_WITH_TIMEOUT(
        model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);
}

// One more emoji rebuilds every chip in the row, and a row scrolled into view
// builds all of them at once. A pop on either would set the whole screen off.
void TestChatPageSelection::chipsTheRowWasBuiltWithDoNotPop() {
    const Chat chat = open("drawn@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    ChatModel *model = chat.model();
    const qlonglong ts =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();

    model->react(ts, "\xf0\x9f\x91\x8d");
    QTRY_VERIFY_WITH_TIMEOUT(
        !model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);

    // A second window builds its rows with the reaction already on them, as a
    // scroll back to an old message does.
    const Chat drawn = alsoOpen("drawn@example.com");
    QVERIFY(drawn.feed);
    QTRY_COMPARE(drawn.count(), 1);
    QQuickItem *chip = nullptr;
    QTRY_VERIFY(drawn.row(0) && (chip = findItem(drawn.row(0), "reactionChip")));

    // Sampled across the length of a pop: one wrongly started would be back at
    // rest by the time a single late check ran.
    QElapsedTimer watching;
    watching.start();
    while (watching.elapsed() < 400) {
        QCOMPARE(chip->scale(), 1.0);
        QTest::qWait(5);
    }

    model->react(ts, "\xf0\x9f\x91\x8d");
    QTRY_VERIFY_WITH_TIMEOUT(
        model->data(model->index(0), ChatModel::ReactionsRole).toMap().isEmpty(), 5000);
}

QTEST_MAIN(TestChatPageSelection)
#include "tst_chatpage_selection.moc"
