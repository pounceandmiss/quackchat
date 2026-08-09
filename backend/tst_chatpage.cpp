// The chat feed's paging, end to end: a real in-memory libtacky seeded with
// more history than one page, a real ChatPage in an offscreen window, and
// assertions on what scrolling actually fetches. A real window is the point -
// contentHeight and originY mean nothing without a laid-out viewport.
//
// This is where the BottomToTop edge convention is pinned down. Row 0 (the
// newest) sits at the content's bottom, so the oldest edge is atYBeginning;
// naming the other one silently turns scroll-up paging into a no-op.
//
// Nothing here connects to a server, so a `before` page whose local read comes
// up short of `limit` reaches for MAM and never answers - tacky documents that
// leg as having no timeout. Every assertion is about pages the local store can
// satisfy alone.
#include <QtTest>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <memory>

#include "AppController.h"
#include "ChatModel.h"
#include "TackyBackend.h"

namespace {
const QString kAcc = QStringLiteral("me@example.com");
constexpr int kPage = 50;               // tacky's default history limit
constexpr int kSeeded = 2 * kPage + 40; // two whole local pages, then a short one
constexpr int kQuiet = 3;               // shorter than any viewport
const QString kStyled = QStringLiteral("*bold* and plain");
const QString kOriginal = QStringLiteral("the original");
// Long enough that a drag across the bubble lands mid-word at both ends.
const QString kSelectable = QStringLiteral("the quick brown fox jumps over the lazy dog");

// Blocks until the backend answers. Requests queue behind the notifies issued
// before them, so this doubles as a barrier for seeding.
QVariant call(TackyBackend &b, const QString &module, const QString &method,
              const QVariantMap &args) {
    QSignalSpy done(&b, &TackyBackend::result);
    const int token = b.request(module, method, args);
    for (int i = 0; i < 100; ++i) {
        for (const QList<QVariant> &sig : std::as_const(done))
            if (sig.at(0).toInt() == token)
                return sig.at(1);
        done.wait(200);
    }
    return {};
}
} // namespace

class TestChatPage : public QObject {
    Q_OBJECT

    // A pop-out window on one chat, closed when it leaves scope.
    struct Chat {
        std::unique_ptr<QObject> window;
        QQuickItem *feed = nullptr;

        ChatModel *model() const {
            return qobject_cast<ChatModel *>(feed->property("model").value<QObject *>());
        }
        int count() const { return feed->property("count").toInt(); }
        qreal prop(const char *name) const { return feed->property(name).toReal(); }

        // olderBuffer()/newerBuffer() are QML functions, not properties, so the
        // scroll handler cannot read a stale copy of them.
        qreal buffer(const char *name) const {
            QVariant out;
            if (!QMetaObject::invokeMethod(feed, name, Q_RETURN_ARG(QVariant, out)))
                return qQNaN();
            return out.toReal();
        }

        QQuickWindow *win() const { return qobject_cast<QQuickWindow *>(window.get()); }

        // Park the viewport `slack` pixels short of the oldest edge, reading
        // that edge afresh because a page that lands moves it.
        void scrollNearOldest(qreal slack = 0) const {
            feed->setProperty("contentY", prop("contentY") - buffer("olderBuffer") + slack);
        }
    };

    qlonglong m_jumpTarget = 0; // first message of jump@, the reply's target
    QQmlEngine *m_engine = nullptr;
    AppController *m_app = nullptr;
    QQmlComponent *m_component = nullptr;

    // Delegates hang off the visual tree, not the QObject one, so findChild
    // never reaches into a row.
    static QQuickItem *findItem(QQuickItem *root, const QString &name) {
        const auto kids = root->childItems();
        for (QQuickItem *kid : kids) {
            if (kid->objectName() == name)
                return kid;
            if (QQuickItem *hit = findItem(kid, name))
                return hit;
        }
        return nullptr;
    }

    Chat open(const QString &jid) {
        Chat chat;
        chat.window.reset(m_component->createWithInitialProperties(
            {{"account", kAcc}, {"chatJid", jid}, {"chatName", "Friend"}}));
        if (auto *win = qobject_cast<QQuickWindow *>(chat.window.get()))
            chat.feed = win->findChild<QQuickItem *>("chatFeed");
        return chat;
    }

    void seed(const QString &jid, int n) {
        for (int i = 0; i < n; ++i)
            m_app->backend()->notify("message", "send",
                                     QVariantMap{{"acc", kAcc},
                                                 {"chat", jid},
                                                 {"body", QString("msg %1").arg(i)}});
    }

    // Long enough for a page to land, so paging that should not happen has had
    // its chance to.
    static void settle() { QTest::qWait(400); }

private slots:
    void initTestCase();
    void cleanupTestCase();

    void oldestRowRendersAboveNewest();
    void buffersNameTheRightEdges();
    void initialLoadStopsAtOnePage();
    void scrollingUpPagesOlder();
    void underTallViewportPagesWithoutScrolling();
    void reachingTheOldestEdgeRetriesAfterExhaustion();
    void bubbleRendersMarkupAsRichText();
    void mouseDragSelectsBodyText();
    void rightClickStillOpensTheBubbleMenu();
    void replyingFromTheComposerThreadsTheTarget();
    void tappingAQuoteJumpsToItsTarget();
};

void TestChatPage::initTestCase() {
    m_engine = new QQmlEngine(this);
    m_app = m_engine->singletonInstance<AppController *>("Quack", "App");
    QVERIFY(m_app);
    // In-memory session (no -transient 0), so nothing touches the real store.
    QVERIFY(m_app->backend()->start());
    m_app->backend()->notify("account", "add",
                             QVariantMap{{"acc", kAcc},
                                         {"password", "x"},
                                         {"domain", "example.com"},
                                         {"username", "me"}});

    seed("friend@example.com", kSeeded);
    seed("quiet@example.com", kQuiet);
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "styled@example.com"},
                                         {"body", kStyled}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "replies@example.com"},
                                         {"body", kOriginal}});

    // jump@ gets the target, then more than a page on top of it, then a reply
    // to the target - so the jump has somewhere to travel.
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "jump@example.com"},
                                         {"body", kOriginal}});
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
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "select@example.com"},
                                         {"body", kSelectable}});

    // Barrier: this result cannot come back before the sends above ran.
    const QVariantList stored =
        call(*m_app->backend(), "message", "history",
             QVariantMap{{"acc", kAcc}, {"chat", "friend@example.com"}, {"limit", 10 * kPage}})
            .toList();
    QCOMPARE(stored.size(), kSeeded);

    m_component = new QQmlComponent(m_engine, "Quack", "ChatWindow", this);
    QVERIFY2(m_component->isReady(), qPrintable(m_component->errorString()));
}

void TestChatPage::cleanupTestCase() {
    if (m_app)
        m_app->backend()->stop();
}

// The convention everything else rests on, asserted against Qt rather than
// assumed: row 0 is the newest message, and it renders below row 1.
void TestChatPage::oldestRowRendersAboveNewest() {
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
void TestChatPage::buffersNameTheRightEdges() {
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
void TestChatPage::initialLoadStopsAtOnePage() {
    const Chat chat = open("friend@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kPage);

    settle();
    QCOMPARE(chat.count(), kPage);
    QVERIFY(chat.buffer("olderBuffer") >= chat.prop("fillThreshold"));
}

// The reported bug: scrolling up loaded nothing, because the edge test named
// the wrong end of the feed.
void TestChatPage::scrollingUpPagesOlder() {
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

// A chat whose whole history is shorter than the viewport cannot be scrolled,
// so the fill has to notice that by itself and ask for more.
void TestChatPage::underTallViewportPagesWithoutScrolling() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);
    QVERIFY(chat.prop("contentHeight") < chat.feed->height());
    QVERIFY(chat.buffer("olderBuffer") < 0.0); // pinned, nothing above

    // The `before` page it fires stays out (no server), so the pill shows.
    QTRY_VERIFY(chat.model()->loadingOlder());
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
void TestChatPage::reachingTheOldestEdgeRetriesAfterExhaustion() {
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

// The model builds the markup; this is the other half - that Qt parses it into
// styled runs instead of drawing the tags.
void TestChatPage::bubbleRendersMarkupAsRichText() {
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

// Whether the body or the swipe handler wins the mouse drag is decided by
// event delivery, so nothing short of a synthesised drag settles it.
void TestChatPage::mouseDragSelectsBodyText() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QVERIFY(body->width() > 0 && body->height() > 0);

    // Window coordinates, on the first line only - across a wrap the release
    // point would sit above the press point.
    const QPointF left = body->mapToScene(QPointF(2, body->height() / 4));
    const QPointF right = body->mapToScene(QPointF(body->width() - 2, body->height() / 4));

    const qreal parked = chat.prop("contentY");
    QTest::mousePress(chat.win(), Qt::LeftButton, {}, left.toPoint());
    QTest::mouseMove(chat.win(), QPointF((left.x() + right.x()) / 2, left.y()).toPoint());
    QTest::mouseMove(chat.win(), right.toPoint());
    QTest::mouseRelease(chat.win(), Qt::LeftButton, {}, right.toPoint());

    const QString picked = body->property("selectedText").toString();
    QVERIFY2(picked.size() > 3, qPrintable(QString("selected only %1").arg(picked)));
    QVERIFY2(kSelectable.contains(picked), qPrintable(picked));

    // A horizontal drag must not have flicked the feed out from under it.
    QCOMPARE(chat.prop("contentY"), parked);
}

// The body is painted over the bubble's right-button MouseArea and has a
// context menu of its own, either of which could swallow the press.
void TestChatPage::rightClickStillOpensTheBubbleMenu() {
    const Chat chat = open("select@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);

    // The Menu is a QObject child of the bubble, and the bubble hangs off the
    // visual tree with its delegate, so climb the parent items to reach it.
    QObject *menu = nullptr;
    for (QQuickItem *at = body; at && !menu; at = at->parentItem())
        menu = at->findChild<QObject *>("bubbleMenu");
    QVERIFY(menu);
    QVERIFY(!menu->property("opened").toBool());

    QTest::mouseClick(chat.win(), Qt::RightButton, {},
                      body->mapToScene(QPointF(body->width() / 2, body->height() / 2)).toPoint());
    QTRY_VERIFY(menu->property("opened").toBool());
}

// The composer used to hold the quoted body and nothing else, so replying sent
// an ordinary message. What has to survive the trip is the target's timestamp.
void TestChatPage::replyingFromTheComposerThreadsTheTarget() {
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

    // ...and the bubble draws the quote it came back with.
    QQuickItem *quote = findItem(chat.feed, "replyQuote");
    QVERIFY(quote);
    QTRY_VERIFY(quote->isVisible());
}

// The whole tappable path against the real store: a reply far above the tail,
// a tap on its quote, and the feed lands on the message it answered.
void TestChatPage::tappingAQuoteJumpsToItsTarget() {
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

    QQuickItem *quote = findItem(chat.feed, "replyQuote");
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

QTEST_MAIN(TestChatPage)
#include "tst_chatpage.moc"
