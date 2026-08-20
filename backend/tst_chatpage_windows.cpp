// What a chat page opens beside itself: the XML viewer, the contact and room
// pages, its own search bar, and the sheets and menus that have to keep clear
// of the system bars. Plus what a row draws for whoever sent it: the name, and
// the face beside it.
#include <QQmlExpression>

#include "AppSettings.h"
#include "AuthorNames.h"
#include "ChatPageTest.h"
#include "SearchModel.h"

namespace {
// The XML viewer is its own top-level window, so it is reached through the
// application rather than down any one chat window's tree. Matched on the
// window and not the page inside it: the mobile sheet hosts that same page off
// whichever chat window declared it.
QQuickWindow *xmlViewerWindow() { return windowNamed("messageXmlWindow"); }

QObject *xmlViewer() {
    QQuickWindow *win = xmlViewerWindow();
    return win ? win->findChild<QObject *>("messageXmlPage") : nullptr;
}

// Not that a viewer opened, but that exactly one did.
int xmlViewerWindows() { return windowsNamed("messageXmlWindow"); }

void closeXmlViewer() {
    if (QQuickWindow *win = xmlViewerWindow())
        win->close();
    QTRY_VERIFY(xmlViewerWindow() == nullptr);
}

// The contact's page is its own top-level window too, reached the same way as
// the XML viewer and for the same reason.
QQuickWindow *contactWindow() { return windowNamed("contactDetailsWindow"); }

// What a room has instead of a contact page, found the same way.
QQuickWindow *roomWindow() { return windowNamed("mucDetailsWindow"); }

constexpr qreal kStatusBar = 60; // the system bars, as Android reports them
constexpr qreal kGestureBar = 90;

// The item every popup is parented into. It carries no objectName, so it goes
// by its class.
QQuickItem *overlayOf(QQuickWindow *win) {
    const auto kids = win->contentItem()->childItems();
    for (QQuickItem *kid : kids)
        if (kid->inherits("QQuickOverlay"))
            return kid;
    return nullptr;
}

// No platform here has system bars, so they are added to the overlay's safe
// area - the margins Android's insets land in, and where the app reads them.
bool fakeSystemBars(QQuickWindow *win, qreal top, qreal bottom) {
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
qreal sceneBottom(QObject *popup) {
    auto *anchor = popup->property("parent").value<QQuickItem *>();
    if (!anchor)
        return qQNaN();
    const qreal y = popup->property("y").toReal();
    return anchor->mapToScene(QPointF(0, y + popup->property("height").toReal())).y();
}
} // namespace

class TestChatPageWindows : public ChatPageTest {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void viewXmlShowsTheStanzaTheStoreHasRecorded();
    void onlyTheWindowThatAskedOpensTheXmlViewer();
    void aFingerScrollsTheStanzaRatherThanSelectingIt();
    void aSheetKeepsItsPageClearOfTheSystemBars();
    void aMenuNearTheBottomOpensClearOfTheSystemBars();
    void tappingTheChatHeaderOpensTheContact();
    void senderNamesComeFromAuthorGet();
    void avatarsMarkTheFootOfEachRun();
    void anOutgoingAvatarHangsOffTheOtherEdge();
    void theAvatarPreferenceClosesTheGutter();
    void closingTheSearchBarEmptiesIt();
    void returnSearchesWhenTheLastAnswerIsGone();
};

void TestChatPageWindows::initTestCase() {
    startBackend();
    seed("quiet@example.com", kQuiet);
    send("pair@example.com", "the older one");
    send("pair@example.com", "the newer one");
    send("room@example.com", "who said that");
    send("avatarroom@example.com", "the one already here");
    send("xml@example.com", "look at my stanza");
    send("twoxml@example.com", "looked at from two windows");
    send("bars@example.com", "under the status bar");
    send("lowmenu@example.com", "at the bottom");

    QCOMPARE(stored("quiet@example.com"), kQuiet);
}

void TestChatPageWindows::cleanupTestCase() { stopBackend(); }

// The menu asks tacky for the stanza and the viewer opens on the answer. This
// runs on a desktop, so the menu opens a window; the sheet that hosts the same
// page on mobile is driven directly at the end, Theme.mobile not being
// forceable here.
void TestChatPageWindows::viewXmlShowsTheStanzaTheStoreHasRecorded() {
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
void TestChatPageWindows::onlyTheWindowThatAskedOpensTheXmlViewer() {
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
void TestChatPageWindows::aFingerScrollsTheStanzaRatherThanSelectingIt() {
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
void TestChatPageWindows::aSheetKeepsItsPageClearOfTheSystemBars() {
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
void TestChatPageWindows::aMenuNearTheBottomOpensClearOfTheSystemBars() {
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

// The header names what you are talking to, so it opens that thing's page: the
// contact behind a conversation of two, the room behind one of many. The
// padlock is a shortcut to the first of them.
void TestChatPageWindows::tappingTheChatHeaderOpensTheContact() {
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

// The name used to be the JID chopped at the "@", which is neither the roster
// name nor a room nick. It comes from author get now, and follows <Changed>.
void TestChatPageWindows::senderNamesComeFromAuthorGet() {
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

namespace {
// One row as the backend hands it over. Nothing is connected, so somebody
// else's message has to be injected rather than received.
QVariantMap said(qlonglong at, const QString &from, const char *body) {
    return QVariantMap{
        {"timestamp", QVariant::fromValue(at)},
        {"from_jid", from},
        {"is_outgoing", false},
        {"content", QVariantMap{{"type", "text"}, {"body", body}}}};
}

// The avatar gutter: the gap between a row's bubble and the edge it sits
// against, whether or not a face is drawn in it. Which edge that is depends on
// the direction, so the caller says.
qreal gutterOf(QQuickItem *row, bool outgoing) {
    QQuickItem *body = findItem(row, "bubbleBody");
    if (!body)
        return qQNaN();
    const qreal left = body->mapToItem(row, QPointF(0, 0)).x();
    return outgoing ? row->width() - (left + body->width()) : left;
}
} // namespace

// One face per run, on the message at its foot; the rest of the run hold the
// gutter open so the bubbles line up.
void TestChatPageWindows::avatarsMarkTheFootOfEachRun() {
    const Chat chat = open("avatarroom@example.com", true);
    QVERIFY(chat.feed);
    QTRY_COMPARE(chat.count(), 1);

    const QString ann = QStringLiteral("avatarroom@example.com/ann");
    const QString bo = QStringLiteral("avatarroom@example.com/bo");
    chat.model()->applyBatch(QVariantList{said(9000000000000001LL, ann, "one"),
                                          said(9000000000000002LL, ann, "two"),
                                          said(9000000000000003LL, ann, "three"),
                                          said(9000000000000004LL, bo, "mine")});
    QTRY_COMPARE(chat.count(), 5);

    // The row is fetched afresh each poll: the delegate for a freshly inserted
    // index is not in place the moment the count changes. On the JID rather
    // than on there being a face at all - the row that held index 0 before the
    // insert had one already, so existence settles before the insert lands.
    auto face = [&](int row) { return findItem(chat.row(row), "messageAvatar"); };
    auto faceJid = [&](int row) {
        QQuickItem *f = face(row);
        return f ? f->property("jid").toString() : QString();
    };

    // Newest first, so row 0 is bo's lone message and rows 1-3 are ann's run
    // with its foot - the lowest of the three on screen - at row 1.
    QTRY_COMPARE(faceJid(0), bo);
    // A room's occupant JID keeps its resource, which is what gives ann and bo
    // a face each rather than the room's logo twice.
    QCOMPARE(faceJid(1), ann);

    // The two above it are the same voice carrying on. The slot is checked too,
    // so an inactive loader is not read as a row that never got built.
    for (int row : {2, 3}) {
        QVERIFY(findItem(chat.row(row), "avatarSlot"));
        QVERIFY2(!face(row),
                 qPrintable(QString("row %1 repeats the face above it").arg(row)));
    }

    // The bubble starts clear of the face rather than over it.
    const qreal foot = gutterOf(chat.row(1), false);
    QVERIFY(foot >= face(1)->mapToItem(chat.row(1), QPointF(0, 0)).x()
                        + face(1)->width());
    // And the rest of the run line up with it, which is what the empty gutter
    // is held open for.
    QCOMPARE(gutterOf(chat.row(2), false), foot);
    QCOMPARE(gutterOf(chat.row(3), false), foot);
}

// Ours sit on the right, so their faces do too - the gutter is on whichever
// side the bubble already took.
void TestChatPageWindows::anOutgoingAvatarHangsOffTheOtherEdge() {
    const Chat chat = open("quiet@example.com");
    QTRY_COMPARE(chat.count(), kQuiet);

    // Every one of them is ours, so the three are a single run.
    auto face = [&](int row) { return findItem(chat.row(row), "messageAvatar"); };
    QTRY_VERIFY(face(0));
    QVERIFY(!face(1));
    QVERIFY(!face(2));

    QQuickItem *row = chat.row(0);
    QQuickItem *mine = face(0);
    const qreal left = mine->mapToItem(row, QPointF(0, 0)).x();
    QVERIFY2(left > row->width() / 2,
             qPrintable(QString("our own face is at %1 of %2, on the wrong side")
                            .arg(left).arg(row->width())));
    QCOMPARE(left + mine->width(), row->width());
    // And the bubble is held off that same edge, so the two never overlap.
    QVERIFY(gutterOf(row, true) > 0);
    QQuickItem *body = findItem(row, "bubbleBody");
    QVERIFY(body);
    QVERIFY(body->mapToItem(row, QPointF(0, 0)).x() + body->width() <= left);
}

// Turned off, there is no face and no gutter held open for one.
void TestChatPageWindows::theAvatarPreferenceClosesTheGutter() {
    const Chat chat = open("avatarroom@example.com", true);
    QTRY_COMPARE(chat.count(), 1);

    const QString ann = QStringLiteral("avatarroom@example.com/ann");
    chat.model()->applyBatch(QVariantList{said(9000000000000001LL, ann, "one")});
    QTRY_COMPARE(chat.count(), 2);
    auto annsFace = [&] {
        QQuickItem *f = findItem(chat.row(0), "messageAvatar");
        return f ? f->property("jid").toString() : QString();
    };
    QTRY_COMPARE(annsFace(), ann);
    QVERIFY(gutterOf(chat.row(0), false) > 0);

    // Arriving from the store, which is how a window that was already open
    // hears about a preference changed in another one.
    m_app->settings()->handleEvent(
        "setting", "Changed",
        QVariantMap{{"key", "chat_avatars"}, {"value", "0"}});
    QTRY_VERIFY(!findItem(chat.row(0), "messageAvatar"));
    QCOMPARE(gutterOf(chat.row(0), false), 0.0);

    // Put back, so the suites that run after this one see the shipped default.
    m_app->settings()->handleEvent(
        "setting", "Changed",
        QVariantMap{{"key", "chat_avatars"}, {"value", "1"}});
    QTRY_COMPARE(annsFace(), ann);
}

// The bar comes back blank. It used to keep the last query with nothing behind
// it: the text never changed, so nothing re-ran it, and the counter and both
// chevrons read as a search that never happened.
void TestChatPageWindows::closingTheSearchBarEmptiesIt() {
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
void TestChatPageWindows::returnSearchesWhenTheLastAnswerIsGone() {
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

QTEST_MAIN(TestChatPageWindows)
#include "tst_chatpage_windows.moc"
