// What the feed does with more history than fits: which edge is which, what a
// scroll fetches, and where a jump lands.
//
// This is where the BottomToTop edge convention is pinned down. Row 0 (the
// newest) sits at the content's bottom, so the oldest edge is atYBeginning;
// naming the other one silently turns scroll-up paging into a no-op.
//
// Nothing here connects to a server, so a `before` page whose local read comes
// up short of `limit` reaches for the archive, and tacky buffers that query
// until there is a stream to carry it - which never comes. Every assertion is
// about pages the local store can satisfy alone.
#include "ChatPageTest.h"

constexpr int kCentre = 1; // ListView.Center, which QML names and C++ does not

class TestChatPagePaging : public ChatPageTest {
    Q_OBJECT

    qlonglong m_jumpTarget = 0; // first message of jump@, the reply's target

private slots:
    void initTestCase();
    void cleanupTestCase();

    void oldestRowRendersAboveNewest();
    void buffersNameTheRightEdges();
    void initialLoadStopsAtOnePage();
    void scrollingUpPagesOlder();
    void scrollingUpOffersTheWayBackDown();
    void underTallViewportPagesWithoutScrolling();
    void reachingTheOldestEdgeRetriesAfterExhaustion();
    void tappingAQuoteJumpsToItsTarget();
    void aShortJumpSlidesAndALongOneCuts();
    void plainMessageDrawsNoQuote();
    void oneSessionServesEveryWindowOnAChat();
};

void TestChatPagePaging::initTestCase() {
    startBackend();
    seed("friend@example.com", kSeeded);
    seed("quiet@example.com", kQuiet);
    seed("shared@example.com", kQuiet);

    // jump@ gets the target, then more than a page on top of it, then a reply
    // to the target - so the jump has somewhere to travel.
    send("jump@example.com", kOriginal);
    m_jumpTarget = call(*m_app->backend(), "message", "history",
                        QVariantMap{{"acc", kAcc}, {"chat", "jump@example.com"}})
                       .toList()
                       .first()
                       .toMap()
                       .value("timestamp")
                       .toLongLong();
    QVERIFY(m_jumpTarget > 0);
    seed("jump@example.com", kPage + 10);
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "jump@example.com"},
                                         {"body", "answering the first"},
                                         {"reply_to_ts", m_jumpTarget}});

    QCOMPARE(stored("friend@example.com"), kSeeded);
}

void TestChatPagePaging::cleanupTestCase() { stopBackend(); }

// The convention everything else rests on, asserted against Qt rather than
// assumed: row 0 is the newest message, and it renders below row 1.
void TestChatPagePaging::oldestRowRendersAboveNewest() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);

    QQuickItem *newest = nullptr;
    QQuickItem *older = nullptr;
    QVERIFY(QMetaObject::invokeMethod(chat.feed, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, newest), Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(chat.feed, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, older), Q_ARG(int, 1)));
    QVERIFY(newest);
    QVERIFY(older);
    QVERIFY2(older->y() < newest->y(),
             "BottomToTop must render older rows above newer ones");

    ChatModel *model = chat.model();
    QVERIFY(model);
    QVERIFY(model->data(model->index(0), ChatModel::TimestampRole).toLongLong() >
            model->data(model->index(1), ChatModel::TimestampRole).toLongLong());
}

// olderBuffer is the unseen history above the viewport, newerBuffer the unseen
// messages below it. Swapping the two is the bug this file guards.
void TestChatPagePaging::buffersNameTheRightEdges() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);
    QVERIFY2(chat.prop("contentHeight") > chat.feed->height(),
             "one seeded page must overflow the viewport");

    // A fresh chat opens on the tail: nothing newer, a page of older above.
    QCOMPARE(chat.buffer("newerBuffer"), 0.0);
    QVERIFY(chat.feed->property("atYEnd").toBool());
    QVERIFY(chat.buffer("olderBuffer") > 0.0);

    // At the far top the roles swap, and zero there means Flickable agrees.
    chat.scrollNearOldest();
    QCOMPARE(chat.buffer("olderBuffer"), 0.0);
    QVERIFY(chat.feed->property("atYBeginning").toBool());
    QVERIFY(chat.buffer("newerBuffer") > 0.0);
}

// Sitting at the tail must not walk the whole archive in: the fill stops once
// there is a screenful of older content above the viewport.
void TestChatPagePaging::initialLoadStopsAtOnePage() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);

    settle();
    QCOMPARE(chat.count(), kPage);
    QVERIFY(chat.buffer("olderBuffer") >= chat.prop("fillThreshold"));
}

// The reported bug: scrolling up loaded nothing, because the edge test named
// the wrong end of the feed.
void TestChatPagePaging::scrollingUpPagesOlder() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);
    settle();
    QCOMPARE(chat.count(), kPage);

    chat.scrollNearOldest();
    QTRY_COMPARE_WITH_TIMEOUT(chat.count(), 2 * kPage, 5000);

    // Reading back does not leave the tail, so live messages keep landing.
    QVERIFY(chat.model()->atTail());
}

// Reading back through history puts the newest message off screen without the
// window losing the tail, which is the commonest way to want the button. Taking
// it scrolls the view back rather than fetching the newest page again.
void TestChatPagePaging::scrollingUpOffersTheWayBackDown() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), kPage);
    settle();

    QQuickItem *jump = chat.feed->findChild<QQuickItem *>("jumpToLatest");
    QVERIFY(jump);
    QVERIFY(!jump->isVisible()); // the newest message is already on screen

    chat.scrollNearOldest();
    QTRY_VERIFY(jump->isVisible());
    QVERIFY(chat.model()->atTail()); // only the view has left the newest row
    QTRY_COMPARE_WITH_TIMEOUT(chat.count(), 2 * kPage, 5000);
    settle();
    const int rows = chat.count();

    QTest::mouseClick(chat.win(), Qt::LeftButton, {},
                      jump->mapToScene(QPointF(jump->width() / 2, jump->height() / 2)).toPoint());
    QTRY_VERIFY(!jump->isVisible());
    QVERIFY(chat.buffer("newerBuffer") < 10);
    // Same window as before the click, so it scrolled instead of reloading.
    QCOMPARE(chat.count(), rows);
}

// A chat whose whole history is shorter than the viewport cannot be scrolled,
// so the fill has to notice that by itself and ask for more.
void TestChatPagePaging::underTallViewportPagesWithoutScrolling() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);
    QVERIFY(chat.prop("contentHeight") < chat.feed->height());
    QVERIFY(chat.buffer("olderBuffer") < 0.0); // pinned, nothing above

    // The `before` page it fires stays out (no stream to carry it), so the
    // pill shows - as waiting on the network, not as loading.
    QTRY_VERIFY(chat.model()->loadingOlder());
    QVERIFY(!chat.model()->online());
    QQuickItem *pill = chat.feed->findChild<QQuickItem *>("olderPill");
    QVERIFY(pill);
    QTRY_VERIFY(pill->isVisible());
    // On the viewport, not the scrolling content, so it neither moves the
    // oldest edge nor rides away from it.
    QCOMPARE(pill->parentItem(), chat.feed);
    QVERIFY(pill->y() >= 0 && pill->y() < chat.feed->height());
}

// An empty page only proves the archive was dry at that moment, so arriving at
// the oldest edge clears the latch and lets one more `before` request out.
void TestChatPagePaging::reachingTheOldestEdgeRetriesAfterExhaustion() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);
    settle();

    chat.feed->setProperty("olderExhausted", true);
    chat.scrollNearOldest(20); // short of the edge: latch holds, nothing fetched
    QVERIFY(chat.feed->property("olderExhausted").toBool());
    settle();
    QCOMPARE(chat.count(), kPage);

    chat.scrollNearOldest();
    QVERIFY(!chat.feed->property("olderExhausted").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(chat.count(), 2 * kPage, 5000);
}

// The whole tappable path against the real store: a reply far above the tail,
// a tap on its quote, and the feed lands on the message it answered.
void TestChatPagePaging::tappingAQuoteJumpsToItsTarget() {
    const Chat chat = open("jump@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), kPage);
    settle();
    ChatModel *model = chat.model();

    // Row 0 is the reply, seeded last; its target is the very first message,
    // well outside the page the chat opened on.
    const qlonglong replyTs =
        model->data(model->index(0), ChatModel::TimestampRole).toLongLong();
    QCOMPARE(model->data(model->index(0), ChatModel::ReplyBodyRole).toString(), kOriginal);
    QCOMPARE(model->rowOfTimestamp(m_jumpTarget), -1); // not in the open window

    QVERIFY(chat.row(0));
    QQuickItem *quote = findItem(chat.row(0), "replyQuote");
    QVERIFY(quote);
    QVERIFY(quote->isVisible());
    const QPointF hit = quote->mapToScene(QPointF(quote->width() / 2, quote->height() / 2));
    QTest::mouseClick(chat.win(), Qt::LeftButton, {}, hit.toPoint());

    // The slice replaces the window and the target is in it, centred.
    QTRY_VERIFY_WITH_TIMEOUT(model->rowOfTimestamp(m_jumpTarget) >= 0, 5000);
    QVERIFY(!model->atTail());
    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QCOMPARE(page->property("highlightTs").toLongLong(), m_jumpTarget);

    // Off the tail, the way back is offered.
    QQuickItem *jump = chat.feed->findChild<QQuickItem *>("jumpToLatest");
    QVERIFY(jump);
    QTRY_VERIFY(jump->isVisible());

    // Taking it reloads the newest page and puts the feed back on the tail.
    QTest::mouseClick(chat.win(), Qt::LeftButton, {},
                      jump->mapToScene(QPointF(jump->width() / 2, jump->height() / 2)).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(model->atTail(), 5000);
    QCOMPARE(model->rowOfTimestamp(replyTs), 0);
}

// Two jumps, told apart by how far they go. One inside the loaded window has
// rows all the way there and slides across them; one that replaces the window
// has nothing continuous to cross, so it lands where it lands.
void TestChatPagePaging::aShortJumpSlidesAndALongOneCuts() {
    const Chat chat = open("jump@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), kPage);
    settle();
    ChatModel *model = chat.model();

    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    QObject *anim = chat.feed->findChild<QObject *>("hitScroll");
    QVERIFY(anim);
    QSignalSpy slides(anim, SIGNAL(started()));

    // Where centring row 12 leaves the view, learned by going there and then
    // putting it back, so the jump has the trip still to make.
    const qreal atTail = chat.prop("contentY");
    QVERIFY(QMetaObject::invokeMethod(chat.feed, "positionViewAtIndex",
                                      Q_ARG(int, 12), Q_ARG(int, kCentre)));
    const qreal centred = chat.prop("contentY");
    chat.feed->setProperty("contentY", atTail);
    QVERIFY(qAbs(centred - atTail) > 50); // far enough that a slide would show

    const qlonglong nearTs =
        model->data(model->index(12), ChatModel::TimestampRole).toLongLong();
    QVERIFY(jumpTo(page, nearTs));

    // The row is loaded already, so the window is left alone and the view
    // travels over it.
    QTRY_COMPARE(page->property("highlightTs").toLongLong(), nearTs);
    QTRY_COMPARE(slides.count(), 1);
    QTRY_VERIFY(!anim->property("running").toBool());
    QCOMPARE(model->rowOfTimestamp(nearTs), 12);
    QVERIFY(qAbs(chat.prop("contentY") - centred) < 2);

    // The reply's target is a slice away.
    QCOMPARE(model->rowOfTimestamp(m_jumpTarget), -1);
    QVERIFY(jumpTo(page, m_jumpTarget));
    QTRY_VERIFY_WITH_TIMEOUT(model->rowOfTimestamp(m_jumpTarget) >= 0, 5000);
    QTRY_COMPARE(page->property("highlightTs").toLongLong(), m_jumpTarget);
    settle();
    QCOMPARE(slides.count(), 1); // no second slide: that one was a cut
}

// The other half of replyRolesAreAlwaysStrings, where it actually showed:
// a message that answers nothing must draw no quote block.
void TestChatPagePaging::plainMessageDrawsNoQuote() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);

    QVERIFY(chat.row(0));
    QQuickItem *quote = findItem(chat.row(0), "replyQuote");
    QVERIFY(quote);
    QVERIFY(!quote->isVisible());
}

// A conversation belongs to the app, not to a window showing it. Two windows on
// one chat read the same model - two would mean two paging cursors over the same
// history - and the message half-typed into one is waiting in the next, which is
// what carries a draft out of the shell and into a pop-out.
//
// The draft is per chat for the same reason: the composer used to keep whatever
// was in it when you left, and hand it to whoever you opened next.
void TestChatPagePaging::oneSessionServesEveryWindowOnAChat() {
    const QString jid = QStringLiteral("shared@example.com");
    const Chat first = open(jid);
    QVERIFY(first.feed);
    QTRY_COMPARE(first.count(), kQuiet);

    auto *input = first.win()->findChild<QObject *>("messageInput");
    QVERIFY(input);
    input->setProperty("text", "half a thought");

    const Chat second = alsoOpen(jid);
    QVERIFY(second.feed);
    QTRY_VERIFY(second.model() != nullptr);
    QCOMPARE(second.model(), first.model());

    auto *echo = second.win()->findChild<QObject *>("messageInput");
    QVERIFY(echo);
    QCOMPARE(echo->property("text").toString(), QString("half a thought"));

    // Point the first window at another conversation and the composer is that
    // one's, empty - not the thought left behind in this one.
    first.window->setProperty("chatJid", "elsewhere@example.com");
    QTRY_COMPARE(input->property("text").toString(), QString());

    // Which is still there for the chat it was written in.
    first.window->setProperty("chatJid", jid);
    QTRY_COMPARE(input->property("text").toString(), QString("half a thought"));
}

QTEST_MAIN(TestChatPagePaging)
#include "tst_chatpage_paging.moc"
