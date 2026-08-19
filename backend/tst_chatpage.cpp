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
// up short of `limit` reaches for the archive, and tacky buffers that query
// until there is a stream to carry it - which never comes. Every assertion is
// about pages the local store can satisfy alone.
#include <QtTest>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>

#include <memory>

#include "AppController.h"
#include "AuthorNames.h"
#include "ChatModel.h"
#include "SearchModel.h"
#include "TackyBackend.h"

namespace {
const QString kAcc = QStringLiteral("me@example.com");
constexpr int kPage = 50;               // tacky's default history limit
constexpr int kSeeded = 2 * kPage + 40; // two whole local pages, then a short one
constexpr int kQuiet = 3;               // shorter than any viewport
constexpr qreal kStatusBar = 60;        // the system bars, as Android reports them
constexpr qreal kGestureBar = 90;
constexpr int kCentre = 1;              // ListView.Center, which QML names and C++ does not
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

// How the mouse picks words out of a message. The first line only: across a
// wrap the release point would sit above the press point.
QString dragThroughWords(QQuickWindow *win, QQuickItem *body) {
    const QPointF left = body->mapToScene(QPointF(2, body->height() / 4));
    const QPointF right = body->mapToScene(QPointF(body->width() - 2, body->height() / 4));
    QTest::mousePress(win, Qt::LeftButton, {}, left.toPoint());
    QTest::mouseMove(win, QPointF((left.x() + right.x()) / 2, left.y()).toPoint());
    QTest::mouseMove(win, right.toPoint());
    QTest::mouseRelease(win, Qt::LeftButton, {}, right.toPoint());
    return body->property("selectedText").toString();
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

        // The delegate for one row. Every row carries the same objectNames, so
        // a search from the feed would answer with whichever it reached first.
        QQuickItem *row(int index) const {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(feed, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, index));
            return item;
        }

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

    // A popup declared inside a bubble is a QObject child of it, and the bubble
    // hangs off the visual tree with its delegate, so climb the parent items to
    // reach it.
    static QObject *popupIn(QQuickItem *from, const QString &name) {
        for (QQuickItem *at = from; at; at = at->parentItem())
            if (QObject *hit = at->findChild<QObject *>(name))
                return hit;
        return nullptr;
    }

    // The jump a search hit and a tapped quote both arrive by. No matches: what
    // a row marks is its own affair, and none of these ask it to mark anything.
    static bool jumpTo(QObject *page, qlonglong ts) {
        return QMetaObject::invokeMethod(page, "jumpTo", Q_ARG(QVariant, QVariant(ts)),
                                         Q_ARG(QVariant, QVariant(QVariantList{})));
    }

    // Opens a chat from scratch: sessions are cached app-wide and shared by
    // every window on a chat, so one an earlier test left loaded would hand this
    // one a feed already full of history.
    Chat open(const QString &jid, bool groupchat = false) {
        m_app->forgetChat(kAcc, jid);
        return alsoOpen(jid, groupchat);
    }

    // A second window on a chat that is already open, sharing its session.
    Chat alsoOpen(const QString &jid, bool groupchat = false) {
        Chat chat;
        chat.window.reset(m_component->createWithInitialProperties(
            {{"account", kAcc},
             {"chatJid", jid},
             {"chatName", "Friend"},
             {"chatGroupchat", groupchat}}));
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

    // The padlock is only drawn once the backend has said what the chat's
    // setting is, so anything reading or clicking it has to wait for that.
    static QQuickItem *shownLock(QQuickWindow *win) {
        auto *lock = win->findChild<QQuickItem *>("omemoToggle");
        if (lock)
            QTest::qWaitFor(
                [&] {
                    // Width as well: it lays out from zero as the control
                    // appears, and a coordinate taken before then misses.
                    return lock->property("visible").toBool() &&
                           lock->width() > 0;
                },
                5000);
        return lock;
    }

    // The XML viewer is its own top-level window, so it is reached through the
    // application rather than down any one chat window's tree. Matched on the
    // window and not the page inside it: the mobile sheet hosts that same page
    // off whichever chat window declared it.
    static QQuickWindow *xmlViewerWindow() {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow *w : windows)
            if (w->objectName() == QLatin1String("messageXmlWindow"))
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }

    static QObject *xmlViewer() {
        QQuickWindow *win = xmlViewerWindow();
        return win ? win->findChild<QObject *>("messageXmlPage") : nullptr;
    }

    // Not that a viewer opened, but that exactly one did.
    static int xmlViewerWindows() {
        int n = 0;
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow *w : windows)
            if (w->objectName() == QLatin1String("messageXmlWindow"))
                ++n;
        return n;
    }

    static void closeXmlViewer() {
        if (QQuickWindow *win = xmlViewerWindow())
            win->close();
        QTRY_VERIFY(xmlViewerWindow() == nullptr);
    }

    // The contact's page is its own top-level window too, reached the same way
    // as the XML viewer and for the same reason.
    static QQuickWindow *contactWindow() {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow *w : windows)
            if (w->objectName() == QLatin1String("contactDetailsWindow"))
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }

    // What a room has instead of a contact page, found the same way.
    static QQuickWindow *roomWindow() {
        const QWindowList windows = QGuiApplication::topLevelWindows();
        for (QWindow *w : windows)
            if (w->objectName() == QLatin1String("mucDetailsWindow"))
                return qobject_cast<QQuickWindow *>(w);
        return nullptr;
    }

    // The item every popup is parented into. It carries no objectName, so it
    // goes by its class.
    static QQuickItem *overlayOf(QQuickWindow *win) {
        const auto kids = win->contentItem()->childItems();
        for (QQuickItem *kid : kids)
            if (kid->inherits("QQuickOverlay"))
                return kid;
        return nullptr;
    }

    // No platform here has system bars, so they are added to the overlay's safe
    // area - the margins Android's insets land in, and where the app reads them.
    static bool fakeSystemBars(QQuickWindow *win, qreal top, qreal bottom) {
        QQuickItem *overlay = overlayOf(win);
        if (!overlay)
            return false;
        QQmlExpression expr(qmlContext(win), overlay,
                            QStringLiteral("SafeArea.additionalMargins = "
                                           "({left: 0, top: %1, right: 0, bottom: %2})")
                                    .arg(top)
                                    .arg(bottom));
        expr.evaluate();
        return !expr.hasError();
    }

    // A popup's y is in the coordinates of whatever it was popped up over.
    static qreal sceneBottom(QObject *popup) {
        auto *anchor = popup->property("parent").value<QQuickItem *>();
        if (!anchor)
            return qQNaN();
        const qreal y = popup->property("y").toReal();
        return anchor->mapToScene(QPointF(0, y + popup->property("height").toReal())).y();
    }

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
    void theComposerGrowsWithTheTextUpToACeiling();
    void enterSendsAndShiftEnterOpensALine();
    void replyingFromTheComposerThreadsTheTarget();
    void tappingAQuoteJumpsToItsTarget();
    void aShortJumpSlidesAndALongOneCuts();
    void plainMessageDrawsNoQuote();
    void ticksFollowBothHops();
    void padlockFollowsTheRowStamp();
    void exposedMessagesFollowTheChatsLock();
    void composerLockIsHiddenInRooms();
    void aTouchTapOnTheLockOnlyFlipsIt();
    void resendGatingFollowsTheRow();
    void onlyAFailedEncryptionOffersThePlaintextWayOut();
    void viewXmlShowsTheStanzaTheStoreHasRecorded();
    void onlyTheWindowThatAskedOpensTheXmlViewer();
    void aFingerScrollsTheStanzaRatherThanSelectingIt();
    void aSheetKeepsItsPageClearOfTheSystemBars();
    void aMenuNearTheBottomOpensClearOfTheSystemBars();
    void togglingTheComposerLockChangesWhatIsSent();
    void keysOpenFromTheComposerLock();
    void tappingTheChatHeaderOpensTheContact();
    void reactionChipsShowTheBackendSet();
    void hoveringAReactionGrowsItsGlyphRatherThanTheItem();
    void aReactionThatArrivesPopsItsChip();
    void chipsTheRowWasBuiltWithDoNotPop();
    void senderNamesComeFromAuthorGet();
    void oneSessionServesEveryWindowOnAChat();
    void closingTheSearchBarEmptiesIt();
    void returnSearchesWhenTheLastAnswerIsGone();
    // Stops the shared backend, so nothing may run after it.
    void theFailedPillCarriesTheBackendsWords();
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
    seed("shared@example.com", kQuiet);
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "styled@example.com"},
                                         {"body", kStyled}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "replies@example.com"},
                                         {"body", kOriginal}});

    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "react@example.com"},
                                         {"body", "react to me"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "chip@example.com"},
                                         {"body", "already reacted to"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "pop@example.com"},
                                         {"body", "watch the chip land"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "drawn@example.com"},
                                         {"body", "reacted to earlier"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "room@example.com"},
                                         {"body", "who said that"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "xml@example.com"},
                                         {"body", "look at my stanza"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "twoxml@example.com"},
                                         {"body", "looked at from two windows"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "bars@example.com"},
                                         {"body", "under the status bar"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "lowmenu@example.com"},
                                         {"body", "at the bottom"}});

    // clear@ turns OMEMO off before its send, so it is the one chat whose rows
    // come back unstamped - every other send here is encrypted by default.
    m_app->backend()->notify("omemo", "setEnabled",
                             QVariantMap{{"acc", kAcc},
                                         {"jid", "clear@example.com"},
                                         {"value", 0}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "clear@example.com"},
                                         {"body", "in the open"}});

    // mixed@ holds the combination that gets remarked on: a message sent while
    // encryption was off, in a chat that has since been set to encrypt.
    m_app->backend()->notify("omemo", "setEnabled",
                             QVariantMap{{"acc", kAcc},
                                         {"jid", "mixed@example.com"},
                                         {"value", 0}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "mixed@example.com"},
                                         {"body", "sent before the lock went on"}});
    m_app->backend()->notify("omemo", "setEnabled",
                             QVariantMap{{"acc", kAcc},
                                         {"jid", "mixed@example.com"},
                                         {"value", 1}});

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
    // Two of them, so a selection can grow past the one a press started it on.
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "pair@example.com"},
                                         {"body", "the older one"}});
    m_app->backend()->notify("message", "send",
                             QVariantMap{{"acc", kAcc},
                                         {"chat", "pair@example.com"},
                                         {"body", "the newer one"}});

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

// A search mark is markup over a body that has none of its own, so a marked
// row turns rich and falls back to plain once the mark moves on. Going back is
// the direction that breaks: Qt fills the plain document with the rich one
// serialised, and the reader gets the HTML instead of the message.
void TestChatPage::bubbleGoesBackToPlainWhenAMarkLeaves() {
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
void TestChatPage::mouseDragSelectsBodyText() {
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
void TestChatPage::onlyOneMessageKeepsItsPickedWords() {
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
void TestChatPage::theBubbleMenuTakesWhatTheDragPickedOut() {
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
void TestChatPage::clickingTheWordsPicksTheMessageWhileSelecting() {
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
void TestChatPage::wordsStayDeafUntilTheirMessageIsPicked() {
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
void TestChatPage::pickedWordsAreLetGoWithTheirMessage() {
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
void TestChatPage::rightClickStillOpensTheBubbleMenu() {
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
void TestChatPage::aTouchTapOpensTheBubbleMenu() {
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
void TestChatPage::aPressOutsideAMenuOnlyDismissesIt() {
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
void TestChatPage::aLongPressSelectsAndOffersItsActions() {
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
void TestChatPage::theSelectionBarActsOnTheOneMessage() {
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
void TestChatPage::takingAReactionDropsThePressedSelection() {
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
void TestChatPage::pressingASelectedMessageAgainHandsOverItsWords() {
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
void TestChatPage::tappingAnotherMessageAddsItToTheSelection() {
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
void TestChatPage::tappingAReactionChipDoesNotAlsoOpenTheMenu() {
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
void TestChatPage::theBubbleMenuLeavesTheComposerFocused() {
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

// The composer was one line tall however much was written into it. It follows
// the text now, up to a ceiling it scrolls past rather than grows through.
void TestChatPage::theComposerGrowsWithTheTextUpToACeiling() {
    const Chat chat = open("composer@example.com");
    QVERIFY(chat.win());

    auto *bar = chat.win()->findChild<QQuickItem *>("composerBar");
    auto *field = chat.win()->findChild<QQuickItem *>("composerField");
    auto *input = chat.win()->findChild<QQuickItem *>("messageInput");
    QVERIFY(bar);
    QVERIFY(field);
    QVERIFY(input);
    QTRY_VERIFY(field->width() > 0);

    // One line at rest, and the bar is the field plus its margins.
    const qreal restField = field->height();
    const qreal restBar = bar->height();
    QVERIFY(restField > 0);
    QCOMPARE(input->property("lineCount").toInt(), 1);
    QCOMPARE(restBar, restField + 20);

    // A long sentence wraps, and the field is taller for it - no newline
    // typed, which is how a message grows it in practice.
    input->setProperty("text", QStringLiteral("wrap me ").repeated(6).trimmed());
    QTRY_VERIFY(input->property("lineCount").toInt() > 1);
    QTRY_VERIFY(field->height() > restField);
    QCOMPARE(bar->height(), field->height() + 20);

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
    }

    // Past the ceiling the field holds still and the text scrolls inside it.
    for (int n = 0; n < 30; ++n)
        lines << QStringLiteral("and more");
    input->setProperty("text", lines.join(QLatin1Char('\n')));
    QTRY_COMPARE(input->property("lineCount").toInt(), lines.size());
    settle();
    QCOMPARE(field->height(), last);
    QVERIFY(input->property("contentHeight").toReal() > field->height());

    // Emptying it puts the bar back where it started.
    input->setProperty("text", QString());
    QTRY_COMPARE(field->height(), restField);
    QCOMPARE(bar->height(), restBar);
}

// Enter still sends, as it did before the field could hold more than a line.
// Shift+Enter is what makes the second one.
void TestChatPage::enterSendsAndShiftEnterOpensALine() {
    const Chat chat = open("newlines@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_VERIFY(chat.model() != nullptr);

    auto *input = chat.win()->findChild<QQuickItem *>("messageInput");
    QVERIFY(input);
    input->forceActiveFocus();
    QTRY_VERIFY(input->hasActiveFocus());

    for (const QChar c : QStringLiteral("first"))
        QTest::keyClick(chat.win(), c.toLatin1());
    QTest::keyClick(chat.win(), Qt::Key_Return, Qt::ShiftModifier);
    for (const QChar c : QStringLiteral("second"))
        QTest::keyClick(chat.win(), c.toLatin1());
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

    // ...and that row's bubble draws the quote it came back with.
    QVERIFY(chat.row(0));
    QQuickItem *quote = findItem(chat.row(0), "replyQuote");
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
void TestChatPage::aShortJumpSlidesAndALongOneCuts() {
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
void TestChatPage::plainMessageDrawsNoQuote() {
    const Chat chat = open("quiet@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), kQuiet);

    QVERIFY(chat.row(0));
    QQuickItem *quote = findItem(chat.row(0), "replyQuote");
    QVERIFY(quote);
    QVERIFY(!quote->isVisible());
}

// The old rule read server_status alone and called anything the server had
// "read", so an unsent message claimed the peer had read it.
void TestChatPage::ticksFollowBothHops() {
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
void TestChatPage::padlockFollowsTheRowStamp() {
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

    // A chat with encryption turned off got what it asked for, so its messages
    // look like any others - the badge is the only difference.
    QQuickItem *encryptedBody = findItem(encrypted.row(0), "bubbleBody");
    QQuickItem *clearBody = findItem(clear.row(0), "bubbleBody");
    QVERIFY(encryptedBody);
    QVERIFY(clearBody);
    QVERIFY(!clearBody->property("flagged").toBool());
    QCOMPARE(clearBody->property("toColor"), encryptedBody->property("toColor"));
}

// The same cleartext message is only worth remarking on while the chat is set
// to encrypt, so the marking follows the switch rather than the message.
void TestChatPage::exposedMessagesFollowTheChatsLock() {
    const Chat chat = open("mixed@example.com");
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);
    QQuickItem *body = findItem(chat.row(0), "bubbleBody");
    QVERIFY(body);
    QTRY_VERIFY(body->property("flagged").toBool());
    QVERIFY(body->property("toColor") != body->property("base"));
    // Settled after the wash has crossed it: a flat fill of the new colour,
    // with the gradient gone. Left on the bubble it would read as a permanent
    // two-tone ramp instead of a colour change.
    QTRY_COMPARE(body->property("sweep").toReal(), 1.0);
    QCOMPARE(body->property("color"), body->property("toColor"));
    QVERIFY(!body->property("gradient").value<QObject *>());
    QQuickItem *wash = findItem(chat.row(0), "bubbleWash");
    QVERIFY(wash);
    QVERIFY(!wash->property("visible").toBool());
    // Turning the chat's padlock off makes it ordinary again, and the colour
    // travels rather than jumping.
    auto *lock = chat.win()->findChild<QQuickItem *>("omemoToggle");
    QVERIFY(lock);
    QTest::mouseClick(chat.win(), Qt::LeftButton, Qt::NoModifier,
                      lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2)).toPoint());
    QTRY_VERIFY(!body->property("flagged").toBool());
    QTRY_COMPARE(body->property("toColor"), body->property("base"));
    QTRY_COMPARE(body->property("sweep").toReal(), 1.0);
    QCOMPARE(body->property("color"), body->property("base"));
    QVERIFY(!wash->property("visible").toBool());
}

// A room's messages go out in the clear whatever the switch says, so it has
// nothing to offer there.
void TestChatPage::composerLockIsHiddenInRooms() {
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

void TestChatPage::resendGatingFollowsTheRow() {
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
void TestChatPage::onlyAFailedEncryptionOffersThePlaintextWayOut() {
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

// The menu asks tacky for the stanza and the viewer opens on the answer. This
// runs on a desktop, so the menu opens a window; the sheet that hosts the same
// page on mobile is driven directly at the end, Theme.mobile not being
// forceable here.
void TestChatPage::viewXmlShowsTheStanzaTheStoreHasRecorded() {
    const Chat chat = open("xml@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *entry = menu->findChild<QQuickItem *>("viewXmlEntry");
    QVERIFY(entry);
    QVERIFY(entry->height() > 0.0);

    // Nothing here reaches a server, so the seeded row was never wired and the
    // store has no stanza for it. The round trip is real all the same: the
    // viewer opens on tacky's answer, saying what the store had.
    //
    // The menu's owner is the bubble, so emitting from there is the same trip
    // the entry makes: bubble signal -> page -> model -> tacky -> window.
    QVERIFY(QMetaObject::invokeMethod(menu->parent(), "viewXmlRequested"));
    QTRY_VERIFY(xmlViewer() != nullptr);
    auto *empty = xmlViewer()->findChild<QQuickItem *>("xmlText");
    QVERIFY(empty);
    QVERIFY(empty->property("text").toString().contains("No stanza"));
    closeXmlViewer();

    // And what the viewer makes of a message the store does have a stanza for.
    // Which stanza tacky answers with is the model's end, driven from
    // tst_chatmodel; this is the half that hosts one.
    const QString laidOut = QStringLiteral("<message to=\"xml@example.com\">\n"
                                           "  <body>look</body>\n"
                                           "</message>");
    auto *pane = chat.win()->findChild<QQuickItem *>("chatPane");
    QVERIFY(pane);
    QVERIFY(QMetaObject::invokeMethod(pane, "showXml",
                                      Q_ARG(QVariant, QVariant(laidOut))));
    QTRY_VERIFY(xmlViewer() != nullptr);

    QObject *viewer = xmlViewer();
    QCOMPARE(viewer->property("xml").toString(), laidOut);
    auto *shown = viewer->findChild<QQuickItem *>("xmlText");
    QVERIFY(shown);
    QCOMPARE(shown->property("text").toString(), laidOut);
    closeXmlViewer();

    // The mobile host is the same page full-screen, so it renders the same way.
    auto *sheet = chat.win()->findChild<QObject *>("xmlSheet");
    QVERIFY(sheet);
    QVERIFY(!sheet->property("opened").toBool());
    sheet->setProperty("xml", laidOut);
    QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
    QTRY_VERIFY(sheet->property("opened").toBool());
    auto *inSheet = chat.win()->findChild<QQuickItem *>("xmlText");
    QVERIFY(inSheet);
    QCOMPARE(inSheet->property("text").toString(), laidOut);
    QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
}

// The stanza is fetched, and the model doing the fetching belongs to the chat
// rather than to a window on it - so its answer reaches every window at once.
// Only the one that asked opens a viewer on it.
void TestChatPage::onlyTheWindowThatAskedOpensTheXmlViewer() {
    const QString jid = QStringLiteral("twoxml@example.com");
    const Chat first = open(jid);
    const Chat second = alsoOpen(jid);
    QVERIFY(first.feed);
    QVERIFY(second.feed);
    QTRY_COMPARE(first.count(), 1);
    QTRY_COMPARE(second.count(), 1);
    QCOMPARE(second.model(), first.model());

    QQuickItem *body = findItem(second.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu->parent(), "viewXmlRequested"));
    QTRY_COMPARE(xmlViewerWindows(), 1);
    // Long enough for the other window to have opened one of its own.
    settle();
    QCOMPARE(xmlViewerWindows(), 1);
    closeXmlViewer();
}

// A stanza is one long line, so reading it means dragging it sideways. A
// TextArea takes that drag for a selection it then never makes, leaving the
// view stuck; under a finger the text gives the drag up. Driven through the
// sheet, which is the host that gets touched.
void TestChatPage::aFingerScrollsTheStanzaRatherThanSelectingIt() {
    const Chat chat = open("bars@example.com");
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    auto *sheet = chat.win()->findChild<QObject *>("xmlSheet");
    QVERIFY(sheet);
    // One line wider than the viewport, so there is somewhere to scroll to.
    sheet->setProperty("xml", QString("<message to='a@h'>%1</message>")
                                  .arg(QString("<body>a long way across</body>")
                                           .repeated(20)));
    QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
    QTRY_VERIFY(sheet->property("opened").toBool());

    auto *xmlPage = chat.win()->findChild<QQuickItem *>("messageXmlPage");
    auto *text = chat.win()->findChild<QQuickItem *>("xmlText");
    auto *scroll = chat.win()->findChild<QQuickItem *>("xmlScroll");
    QVERIFY(xmlPage);
    QVERIFY(text);
    QVERIFY(scroll);
    auto *flick = scroll->property("contentItem").value<QQuickItem *>();
    QVERIFY(flick);
    QTRY_VERIFY(scroll->property("contentWidth").toReal() > scroll->width());

    // A pointer keeps its selection: dragging across the words is how a line
    // of it gets copied.
    QCOMPARE(text->property("selectByMouse").toBool(), true);
    QCOMPARE(text->property("activeFocusOnPress").toBool(), true);

    xmlPage->setProperty("touch", true);
    QCOMPARE(text->property("selectByMouse").toBool(), false);
    // Nothing on this page is typed into, and on Android the software keyboard
    // follows the focus.
    QCOMPARE(text->property("activeFocusOnPress").toBool(), false);

    static QPointingDevice *finger = QTest::createTouchDevice();
    const QPoint from =
        scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2)).toPoint();
    QTest::touchEvent(chat.win(), finger).press(0, from);
    for (int i = 1; i <= 12; ++i) {
        // Consecutive moves are compressed in pairs, so each is sent twice.
        const QPoint to = from - QPoint(10 * i, 0);
        QTest::touchEvent(chat.win(), finger).move(0, to);
        QTest::touchEvent(chat.win(), finger).move(0, to);
    }
    QVERIFY(flick->property("contentX").toReal() > 0);
    QVERIFY(!text->property("activeFocus").toBool());
    QTest::touchEvent(chat.win(), finger).release(0, from - QPoint(120, 0));

    QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
}

// A sheet is parented to the overlay, past the inset the window applies to its
// own content: on Android that put the XML viewer's header under the status
// bar. The sheet still covers the window - only the page inside it moves in.
void TestChatPage::aSheetKeepsItsPageClearOfTheSystemBars() {
    const Chat chat = open("bars@example.com");
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);

    auto *sheet = chat.win()->findChild<QObject *>("xmlSheet");
    QVERIFY(sheet);
    QVERIFY(QMetaObject::invokeMethod(sheet, "open"));
    QTRY_VERIFY(sheet->property("opened").toBool());

    auto *shown = chat.win()->findChild<QQuickItem *>("messageXmlPage");
    QVERIFY(shown);
    QCOMPARE(shown->mapToScene(QPointF(0, 0)).y(), 0.0);

    QVERIFY(fakeSystemBars(chat.win(), kStatusBar, kGestureBar));
    QTRY_COMPARE(shown->mapToScene(QPointF(0, 0)).y(), kStatusBar);
    QCOMPARE(shown->height(), chat.win()->height() - kStatusBar - kGestureBar);
    // The background still paints behind the bars, so nothing bands.
    QCOMPARE(sheet->property("height").toReal(), qreal(chat.win()->height()));

    QVERIFY(QMetaObject::invokeMethod(sheet, "close"));
}

// Qt keeps a menu inside the window, which on Android runs under the gesture
// bar - so one opened near the bottom was cut off by it rather than moved up.
void TestChatPage::aMenuNearTheBottomOpensClearOfTheSystemBars() {
    const Chat chat = open("lowmenu@example.com");
    QVERIFY(chat.feed);
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 1);
    QVERIFY(fakeSystemBars(chat.win(), kStatusBar, kGestureBar));

    QQuickItem *body = findItem(chat.feed, "bubbleText");
    QVERIFY(body);
    QObject *menu = popupIn(body, "bubbleMenu");
    QVERIFY(menu);
    auto *bubble = qobject_cast<QQuickItem *>(menu->parent());
    QVERIFY(bubble);

    // Held at the very bottom edge, where the menu's own height is what does
    // not fit.
    const QPointF atEdge = bubble->mapFromScene(QPointF(0, chat.win()->height() - 4));
    QVERIFY(QMetaObject::invokeMethod(bubble, "openMenu",
                                      Q_ARG(QVariant, QVariant::fromValue(atEdge)),
                                      Q_ARG(QVariant, false)));
    QTRY_VERIFY(menu->property("opened").toBool());

    QVERIFY(menu->property("height").toReal() > 0.0);
    QVERIFY(sceneBottom(menu) <= chat.win()->height() - kGestureBar);
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
}

// The whole path in one go: the padlock in the composer, through tacky's stored
// per-chat setting, to how the next message is actually stamped - and only the
// next one, since the messages already sent keep the terms they went out under.
void TestChatPage::togglingTheComposerLockChangesWhatIsSent() {
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
void TestChatPage::keysOpenFromTheComposerLock() {
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

// The header names what you are talking to, so it opens that thing's page: the
// contact behind a conversation of two, the room behind one of many. The
// padlock is a shortcut to the first of them.
void TestChatPage::tappingTheChatHeaderOpensTheContact() {
    const Chat chat = open("header@example.com");
    QVERIFY(chat.win());
    QVERIFY(!contactWindow());
    auto *identity = chat.win()->findChild<QQuickItem *>("chatIdentity");
    QVERIFY(identity);

    const QPointF centre =
            identity->mapToScene(QPointF(identity->width() / 2, identity->height() / 2));
    QTest::mouseClick(chat.win(), Qt::LeftButton, {}, centre.toPoint());
    QTRY_VERIFY(contactWindow());
    QCOMPARE(contactWindow()->property("jid").toString(),
             QString("header@example.com"));
    contactWindow()->close();
    QTRY_VERIFY(contactWindow() == nullptr);

    // A room has no contact behind it; what it has is the room itself.
    const Chat room = open("headerroom@example.com", true);
    QVERIFY(room.win());
    auto *roomIdentity = room.win()->findChild<QQuickItem *>("chatIdentity");
    QVERIFY(roomIdentity);
    const QPointF roomCentre = roomIdentity->mapToScene(
            QPointF(roomIdentity->width() / 2, roomIdentity->height() / 2));
    QTest::mouseClick(room.win(), Qt::LeftButton, {}, roomCentre.toPoint());
    QTRY_VERIFY(roomWindow());
    QCOMPARE(roomWindow()->property("jid").toString(),
             QString("headerroom@example.com"));
    QVERIFY(!contactWindow());
    roomWindow()->close();
    QTRY_VERIFY(roomWindow() == nullptr);
}

// A touch point carries no button, so the padlock's right-click handler was
// offered every tap: on a phone the keys came up over the switch it flipped.
void TestChatPage::aTouchTapOnTheLockOnlyFlipsIt() {
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

void TestChatPage::reactionChipsShowTheBackendSet() {
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
void TestChatPage::hoveringAReactionGrowsItsGlyphRatherThanTheItem() {
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

void TestChatPage::aReactionThatArrivesPopsItsChip() {
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
void TestChatPage::chipsTheRowWasBuiltWithDoNotPop() {
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

// The name used to be the JID chopped at the "@", which is neither the roster
// name nor a room nick. It comes from author get now, and follows <Changed>.
void TestChatPage::senderNamesComeFromAuthorGet() {
    const Chat chat = open("room@example.com", true);
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    // Someone else's message: nothing is connected, so it is injected rather
    // than received, but the row is the shape the backend hands over.
    const QString from = QStringLiteral("room@example.com/ann");
    ChatModel *model = chat.model();
    model->applyBatch(QVariantList{QVariantMap{
        {"timestamp", QVariant::fromValue<qlonglong>(9000000000000000LL)},
        {"from_jid", from},
        {"is_outgoing", false},
        {"content", QVariantMap{{"type", "text"}, {"body", "who said that"}}}}});
    QTRY_COMPARE(chat.count(), 2);

    // The row is fetched afresh each poll: the delegate for a freshly inserted
    // index is not in place the moment the count changes.
    auto authorLine = [&] { return findItem(chat.row(0), "authorLine"); };
    QTRY_VERIFY(authorLine() && authorLine()->isVisible()); // a room names its voices
    QQuickItem *line = authorLine();
    // Nothing is connected, so the JID stands in until a name arrives.
    QCOMPARE(line->property("text").toString(), from);

    auto *authors = chat.win()->findChild<AuthorNames *>();
    QVERIFY(authors);
    authors->handleEvent("author", "Changed",
                         QVariantMap{{"acc", kAcc},
                                     {"chat", "room@example.com"},
                                     {"from", from},
                                     {"name", "Ann from the room"}});
    QTRY_COMPARE(line->property("text").toString(), QString("Ann from the room"));

    // A 1:1 has only two voices, both already named around the bubble.
    const Chat direct = open("quiet@example.com");
    QTRY_COMPARE(direct.count(), kQuiet);
    QQuickItem *quietLine = findItem(direct.row(0), "authorLine");
    QVERIFY(quietLine);
    QVERIFY(!quietLine->isVisible());
}

// A conversation belongs to the app, not to a window showing it. Two windows on
// one chat read the same model - two would mean two paging cursors over the same
// history - and the message half-typed into one is waiting in the next, which is
// what carries a draft out of the shell and into a pop-out.
//
// The draft is per chat for the same reason: the composer used to keep whatever
// was in it when you left, and hand it to whoever you opened next.
void TestChatPage::oneSessionServesEveryWindowOnAChat() {
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

// The bar comes back blank. It used to keep the last query with nothing behind
// it: the text never changed, so nothing re-ran it, and the counter and both
// chevrons read as a search that never happened.
void TestChatPage::closingTheSearchBarEmptiesIt() {
    const Chat chat = open("pair@example.com");
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 2);

    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    auto *field = chat.win()->findChild<QObject *>("chatSearchField");
    QVERIFY(field);
    SearchModel *search = page->findChild<SearchModel *>();
    QVERIFY(search);

    QVERIFY(QMetaObject::invokeMethod(page, "openSearch"));
    field->setProperty("text", "older");
    QTRY_VERIFY_WITH_TIMEOUT(search->rowCount() == 1, 5000);

    QVERIFY(QMetaObject::invokeMethod(page, "closeSearch"));
    QVERIFY(field->property("text").toString().isEmpty());
    QVERIFY(!search->searched());

    // Still empty on the way back in, with nothing claiming a count.
    QVERIFY(QMetaObject::invokeMethod(page, "openSearch"));
    QVERIFY(field->property("text").toString().isEmpty());
    auto *counter = chat.win()->findChild<QObject *>("hitCounter");
    QVERIFY(counter);
    QCOMPARE(counter->property("text").toString(), QString());
}

// The other way into a query with no answer behind it, now that closing does
// not: a chat switch resets the model under a bar that stays up. Return
// searched only while the debounce was running, so here it stepped a list that
// was not there.
void TestChatPage::returnSearchesWhenTheLastAnswerIsGone() {
    const Chat chat = open("pair@example.com");
    QVERIFY(chat.win());
    QTRY_COMPARE(chat.count(), 2);

    auto *page = chat.win()->findChild<QObject *>("chatPane");
    QVERIFY(page);
    auto *field = chat.win()->findChild<QObject *>("chatSearchField");
    QVERIFY(field);
    SearchModel *search = page->findChild<SearchModel *>();
    QVERIFY(search);

    QVERIFY(QMetaObject::invokeMethod(page, "openSearch"));
    field->setProperty("text", "one");
    QTRY_VERIFY_WITH_TIMEOUT(search->rowCount() == 2, 5000);

    // What the switch leaves: the answer gone, the query still on screen. The
    // debounce stops with it, so Return has nothing pending to run instead.
    QVERIFY(QMetaObject::invokeMethod(page, "dropHits"));
    QVERIFY(!search->searched());
    QCOMPARE(field->property("text").toString(), QString("one"));

    QTest::keyClick(chat.win(), Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(search->rowCount() == 2, 5000);
}

// Must stay last: stopping the backend is what fails the `before` page that
// is out, and nothing after it would have one.
void TestChatPage::theFailedPillCarriesTheBackendsWords() {
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

QTEST_MAIN(TestChatPage)
#include "tst_chatpage.moc"
