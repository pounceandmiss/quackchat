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

        // Park the viewport `slack` pixels short of the oldest edge, reading
        // that edge afresh because a page that lands moves it.
        void scrollNearOldest(qreal slack = 0) const {
            feed->setProperty("contentY", prop("contentY") - buffer("olderBuffer") + slack);
        }
    };

    QQmlEngine *m_engine = nullptr;
    AppController *m_app = nullptr;
    QQmlComponent *m_component = nullptr;

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

QTEST_MAIN(TestChatPage)
#include "tst_chatpage.moc"
