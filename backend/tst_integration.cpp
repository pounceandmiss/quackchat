// Against a REAL libtacky backend (no server needed). `message send` stores
// locally and emits real message/<New> events, exercising the full delivery
// path: backend emit -> queued hop -> TackyBackend signal -> model.
#include <QtTest>
#include <QCryptographicHash>
#include <QSignalSpy>

#include "AvatarController.h"
#include "ChatListModel.h"
#include "ChatModel.h"
#include "MessageMarkup.h"
#include "SearchModel.h"
#include "TackyBackend.h"

namespace {
void addAccount(TackyBackend &b, const QString &acc) {
    b.notify("account", "add",
             QVariantMap{{"acc", acc},
                         {"password", "x"},
                         {"domain", acc.section('@', 1)},
                         {"username", acc.section('@', 0, 0)}});
}

// The reply to `token`, or an invalid QVariant while it is still in flight.
// Replies from every requester land in the same spy, so the token sorts them.
QVariant resultFor(const QSignalSpy &spy, int token) {
    for (const QList<QVariant> &row : spy)
        if (row.at(0).toInt() == token)
            return row.at(1);
    return {};
}
} // namespace

class TestIntegration : public QObject {
    Q_OBJECT
private slots:
    // Mirrors the real connect path: roster load fires chatlist <Changed>, the
    // model refetches, and a chat that exists in the backend shows up.
    void chatListRefreshesOnChanged();
    // Live outgoing message reaches the open ChatModel via message/<New>.
    void chatModelReceivesLiveSent();
    // Styling spans come from tacky's own XEP-0393 parser, not a canned fixture.
    void markupFollowsTackysStylingSpans();
    // Replies are XEP-0461: the backend owns the reference and the quote.
    void replySendCarriesTheTargetAndComesBackResolved();
    // A 1:1 encrypts unless the chat says otherwise, and the row says which.
    void sendsAreStampedOmemoByDefault();
    // Sending one message in the clear rewrites that row's stamp, and only it.
    void plaintextResendClearsTheStamp();
    // Search against the real store: arg names, cursor and result shape are
    // tacky's, and only a round trip proves we speak them.
    void searchFindsWhatWasStored();
    // Avatar bytes survive the JSON transport unchanged.
    void avatarBytesSurviveTheWire();
    // A fresh frontend under a session that kept running learns its hashes.
    void avatarHashSurvivesAFrontendRestart();
    // Jumping around a chat leaves no direction stuck in flight: the pill that
    // rides on loadingOlder has to go out again once the storm settles.
    void jumpStormLeavesNothingInFlight();
    // The newest page of a chat with nothing stored has only the archive to go
    // to, and offline that leg never answers.
    void openingAnUnsyncedChatOfflineStrandsTheInitialPage();
    // And so does scrolling off the end of what is stored, which is where
    // jumping around a chat lands you.
    void pagingPastTheOldestStoredMessageNeverAnswers();
};

void TestIntegration::chatListRefreshesOnChanged() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatListModel chatList;
    chatList.setBackend(&backend);
    chatList.setAccount("me@example.com"); // initial get (empty)

    // Create a local chat by sending; the backend now has a "free" entry that a
    // fresh `chatlist get` will return.
    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "hi"}});

    // Simulate the connect-time signal: roster/bookmark load fires chatlist
    // <Changed> (bare "Changed" on the JSON wire), which must make the model
    // refetch and pick up the new chat.
    chatList.handleEvent("chatlist", "Changed",
                         QVariantMap{{"acc", "me@example.com"}});

    QTRY_VERIFY_WITH_TIMEOUT(chatList.rowCount() == 1, 5000);
    QCOMPARE(chatList.data(chatList.index(0), ChatListModel::JidRole).toString(),
             QString("friend@example.com"));

    backend.stop();
}

void TestIntegration::chatModelReceivesLiveSent() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "hi there"}});

    // The backend emits message/<New>; at-tail gate lets it in.
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 1, 5000);
    QCOMPARE(chat.data(chat.index(0), ChatModel::BodyRole).toString(),
             QString("hi there"));
    QVERIFY(chat.data(chat.index(0), ChatModel::OutgoingRole).toBool());

    backend.stop();
}

// Offsets are tacky's, counted its way, over a body it stripped the styling
// characters out of. A fixture cannot catch the two sides disagreeing; only a
// round trip through the real parser can.
void TestIntegration::markupFollowsTackysStylingSpans() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    // The emoji is the point: it is two UTF-16 units but one code point, so a
    // span after it only lands right if both sides count the same way.
    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", QString::fromUtf8("\U0001F600 *bold* and _soft_")}});

    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 1, 5000);
    // tacky hands back the display body, styling characters removed.
    QCOMPARE(chat.data(chat.index(0), ChatModel::BodyRole).toString(),
             QString::fromUtf8("\U0001F600 bold and soft"));
    QCOMPARE(chat.data(chat.index(0), ChatModel::MarkupRole).toString(),
             QString::fromUtf8("\U0001F600 <b>bold</b> and <i>soft</i>"));

    // Quotes keep their "> " markers and run together across lines, so the
    // whole block is one colored span and the reply below it is not in it.
    chat.setQuoteColor("#0a0");
    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "> they said\n> and then\nmy reply"}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 2, 5000);
    QCOMPARE(chat.data(chat.index(0), ChatModel::BodyRole).toString(),
             QString("> they said\n> and then\nmy reply"));
    QCOMPARE(chat.data(chat.index(0), ChatModel::MarkupRole).toString(),
             QString("<span style=\"color:#0a0\">&gt; they said<br>&gt; and then"
                     "</span><br>my reply"));

    backend.stop();
}

// Sending names the target by timestamp and nothing else; tacky resolves it to
// a reply id, quotes it on the wire, and hands the row back with the preview
// and author already worked out.
void TestIntegration::replySendCarriesTheTargetAndComesBackResolved() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    chat.send("the original\nsecond line");
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 1, 5000);
    const qlonglong target =
        chat.data(chat.index(0), ChatModel::TimestampRole).toLongLong();

    chat.send("my answer", target);
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 2, 5000);

    // The reply's own body is what was typed - the "> " quote goes on the wire
    // as an XEP-0428 fallback, never into the row we draw.
    QCOMPARE(chat.data(chat.index(0), ChatModel::BodyRole).toString(),
             QString("my answer"));
    // The preview is the target's first line only.
    QCOMPARE(chat.data(chat.index(0), ChatModel::ReplyBodyRole).toString(),
             QString("the original"));
    QCOMPARE(chat.data(chat.index(0), ChatModel::ReplyAuthorRole).toString(),
             QString("me@example.com"));

    // A plain send stays plain, so the bubble knows not to draw a quote.
    QVERIFY(chat.data(chat.index(1), ChatModel::ReplyBodyRole).toString().isEmpty());
    QVERIFY(chat.data(chat.index(1), ChatModel::ReplyAuthorRole).toString().isEmpty());

    backend.stop();
}

// The default nobody sets: a 1:1 is encrypted, and turning the chat's switch
// off is what puts a message in the clear. Nothing is connected here, so these
// are stamped but unsent - which is the distinction the padlock draws.
void TestIntegration::sendsAreStampedOmemoByDefault() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "under the lock"}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 1, 5000);
    QCOMPARE(chat.data(chat.index(0), ChatModel::EncryptionRole).toString(),
             QString("omemo"));
    QCOMPARE(chat.data(chat.index(0), ChatModel::ServerStatusRole).toString(),
             QString("pending"));
    QVERIFY(chat.data(chat.index(0), ChatModel::FailReasonRole).toString().isEmpty());

    backend.notify("omemo", "setEnabled",
                   QVariantMap{{"acc", "me@example.com"},
                               {"jid", "friend@example.com"},
                               {"value", 0}});
    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "in the open"}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 2, 5000);
    QVERIFY(chat.data(chat.index(0), ChatModel::EncryptionRole).toString().isEmpty());
    // The switch decides the next message, not the ones already sent.
    QCOMPARE(chat.data(chat.index(1), ChatModel::EncryptionRole).toString(),
             QString("omemo"));

    backend.stop();
}

// The downgrade has to reach the row on screen: a message drawn as encrypted
// that went out in the clear is the worst this can do. The backend reports the
// new stamp on <Status>, which is what takes the padlock off.
void TestIntegration::plaintextResendClearsTheStamp() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    backend.notify("message", "send",
                   QVariantMap{{"acc", "me@example.com"},
                               {"chat", "friend@example.com"},
                               {"body", "let me out"}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 1, 5000);
    QCOMPARE(chat.data(chat.index(0), ChatModel::EncryptionRole).toString(),
             QString("omemo"));

    const qlonglong ts =
        chat.data(chat.index(0), ChatModel::TimestampRole).toLongLong();
    chat.resend(ts, true);
    QTRY_VERIFY_WITH_TIMEOUT(
        chat.data(chat.index(0), ChatModel::EncryptionRole).toString().isEmpty(), 5000);
    QCOMPARE(chat.rowCount(), 1); // rewritten in place, not sent again

    backend.stop();
}

// A chat window seeds the store, then the search reads it back through the same
// backend. The store's matching is tacky's own - what this covers is that the
// request we build reaches it and the result we unpack is the one it sent.
void TestIntegration::searchFindsWhatWasStored() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    for (const QString &body : {QStringLiteral("pizza tonight?"),
                                QStringLiteral("sure, see you then")})
        backend.notify("message", "send",
                       QVariantMap{{"acc", "me@example.com"},
                                   {"chat", "friend@example.com"},
                                   {"body", body}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 2, 5000);

    SearchModel search;
    search.setBackend(&backend);
    search.setAccount("me@example.com");
    search.setChat("friend@example.com");
    search.setQuery("pizza");
    search.search();

    QTRY_VERIFY_WITH_TIMEOUT(search.rowCount() == 1, 5000);
    QCOMPARE(search.data(search.index(0), SearchModel::BodyRole).toString(),
             QString("pizza tonight?"));
    QCOMPARE(search.data(search.index(0), SearchModel::ChatJidRole).toString(),
             QString("friend@example.com"));
    // Where it matched is tacky's answer, in tacky's units, over tacky's own
    // rendering of the body. A fixture cannot catch the two sides counting
    // differently; only the round trip can.
    QCOMPARE(search.data(search.index(0), SearchModel::SnippetRole).toString(),
             QString("<b>pizza</b> tonight?"));
    // One page held everything, so there is nothing behind it to ask for.
    QVERIFY(search.complete());
    QVERIFY(!search.failed());

    // The same query from the whole account finds it too, and its cursor is the
    // pair form - which only round-trips if we never took it apart.
    SearchModel wide;
    wide.setBackend(&backend);
    wide.setAccount("me@example.com");
    wide.setQuery("pizza");
    wide.search();
    QTRY_VERIFY_WITH_TIMEOUT(wide.rowCount() == 1, 5000);
    QCOMPARE(wide.data(wide.index(0), SearchModel::ChatJidRole).toString(),
             QString("friend@example.com"));

    backend.stop();
}

// `avatar publish -data` takes raw bytes, but the transport between here and
// tacky is JSON, which is text, so tacky declares the argument base64 and
// decodes it on arrival. This drives every possible byte through `avatar
// inject` (the same -data, no server needed) and reads it out again. The
// returned hash is tacky's own SHA-1 of what it received, so a match proves it
// decoded to our bytes exactly.
void TestIntegration::avatarBytesSurviveTheWire() {
    QByteArray bytes(256, Qt::Uninitialized);
    for (int i = 0; i < 256; ++i)
        bytes[i] = static_cast<char>(i);

    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    QSignalSpy results(&backend, &TackyBackend::result);
    QSignalSpy errors(&backend, &TackyBackend::error);

    const int injectToken =
        backend.request("avatar", "inject",
                        QVariantMap{{"acc", "me@example.com"},
                                    {"jid", "me@example.com"},
                                    {"data", QString::fromLatin1(bytes.toBase64())}});
    QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(), 5000);
    QVERIFY2(errors.isEmpty(), "inject reported an error");
    QCOMPARE(results.first().at(0).toInt(), injectToken);

    const QString hash = results.first().at(1).toString();
    QCOMPARE(hash, QString(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1)
                               .toHex()));

    // And back out: `avatar data` is base64, the same reply the image provider
    // decodes for display.
    results.clear();
    backend.request("avatar", "data",
                    QVariantMap{{"acc", "me@example.com"}, {"hash", hash}});
    QTRY_VERIFY_WITH_TIMEOUT(!results.isEmpty(), 5000);
    QCOMPARE(QByteArray::fromBase64(results.first().at(1).toString().toLatin1()),
             bytes);

    backend.stop();
}

// The Android reopen: the interpreter belongs to a service the activity does
// not take with it, so a second frontend meets JIDs the session already has
// marked visible. That re-mark is a no-op with no <Update> behind it, leaving
// the `metadata` read as the only thing carrying the hash across the restart.
void TestIntegration::avatarHashSurvivesAFrontendRestart() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    QSignalSpy results(&backend, &TackyBackend::result);

    // The avatar a previous run had already fetched and cached.
    const int inject = backend.request(
        "avatar", "inject",
        QVariantMap{{"acc", "me@example.com"},
                    {"jid", "bob@example.com"},
                    {"data", QString::fromLatin1(QByteArray("bobsface").toBase64())}});
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, inject).isValid(), 5000);
    const QString hash = resultFor(results, inject).toString();
    QVERIFY(!hash.isEmpty());

    // The mark that run left behind.
    AvatarController first;
    first.setBackend(&backend);
    first.hashFor("me@example.com", "bob@example.com"); // the read subscribes
    // Requests are answered in order, so a reply to a later one proves the mark
    // has landed. It also pins the reply shape the controller reads: an object
    // keyed by field, not a bare string.
    const int barrier =
        backend.request("avatar", "metadata",
                        QVariantMap{{"acc", "me@example.com"},
                                    {"jid", "bob@example.com"}});
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(results, barrier).isValid(), 5000);
    QCOMPARE(resultFor(results, barrier).toMap().value("hash").toString(), hash);

    // The UI process dies; the session does not.
    AvatarController second;
    second.setBackend(&backend);
    QCOMPARE(second.hashFor("me@example.com", "bob@example.com"), QString());
    QTRY_COMPARE_WITH_TIMEOUT(
        second.hashFor("me@example.com", "bob@example.com"), hash, 5000);

    backend.stop();
}

// Search steps the feed through its hits one jump at a time, and a key held
// down issues them faster than the pages answering them come back. Every jump
// cancels whatever history was out and the view refills behind it, so the
// directions go in and out of flight in a storm. What must hold at the end of
// it is that none stayed in: loadingOlder is what the "Loading" pill rides on,
// and a direction stuck in m_inflight also refuses every later request for it.
//
// Every page here is one the store can answer: the storm never reaches past
// the oldest stored message, so nothing has cause to go to the archive. That
// is what makes this a test of the cancel/re-issue bookkeeping alone.
void TestIntegration::jumpStormLeavesNothingInFlight() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    // Stored before any model exists, so the window starts as one page rather
    // than being filled to the bottom by the live <New> for each send.
    const int total = 400;
    for (int i = 0; i < total; ++i)
        backend.notify("message", "send",
                       QVariantMap{{"acc", "me@example.com"},
                                   {"chat", "friend@example.com"},
                                   {"body", QStringLiteral("m%1").arg(i)}});
    QSignalSpy stored(&backend, &TackyBackend::result);
    const int barrier = backend.request("message", "history",
                                        QVariantMap{{"acc", "me@example.com"},
                                                    {"chat", "friend@example.com"},
                                                    {"limit", total}});
    QTRY_VERIFY_WITH_TIMEOUT(resultFor(stored, barrier).isValid(), 20000);
    QCOMPARE(resultFor(stored, barrier).toList().size(), total);

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() >= 50, 10000);

    // Aim at rows inside the opening page, so every jump and every fill behind
    // it stays well clear of the oldest stored message.
    QList<qlonglong> targets;
    for (int row = 0; row < 50; row += 7)
        targets << chat.data(chat.index(row), ChatModel::TimestampRole).toLongLong();
    QVERIFY(targets.size() > 3);

    // The storm: jump, page, jump again without waiting for either to land.
    for (int pass = 0; pass < 6; ++pass) {
        for (qlonglong ts : targets) {
            chat.gotoTimestamp(ts);
            chat.loadOlder();
            // One trip round the loop: enough for some replies to arrive
            // mid-storm, not enough for all of them.
            QCoreApplication::processEvents();
        }
    }

    QTRY_VERIFY_WITH_TIMEOUT(!chat.loadingOlder(), 10000);
    // And the window is still usable afterwards: a direction that answered but
    // left its flag up would refuse this silently.
    const int before = chat.rowCount();
    chat.loadOlder();
    QTRY_VERIFY_WITH_TIMEOUT(!chat.loadingOlder(), 10000);
    QVERIFY(chat.rowCount() > before);

    backend.stop();
}

// tacky answers `history` from the store when it can. A chat with nothing
// stored and no cursor is the one case that always reaches for the archive -
// and with no connection the MAM leg is written to a dead stream, so nothing
// ever comes back for it. The direction stays in flight, which lights the pill
// and makes the model refuse every later initial page.
void TestIntegration::openingAnUnsyncedChatOfflineStrandsTheInitialPage() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com"); // added, never signed in

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("stranger@example.com"); // nothing stored for this one

    QVERIFY(chat.loadingOlder()); // the initial page went out

    // Give it far longer than an answer would take. Nothing arrives, and
    // nothing ever will: the request is out for good.
    QTest::qWait(2000);
    QVERIFY2(!chat.loadingOlder(),
             "initial page never came back: loadingOlder is stuck true, which "
             "is the spinning \"Loading\" pill");

    backend.stop();
}

// The same page the model asks for when the user reaches the end of what is
// stored, asked for on its own with no jumping anywhere near it. It is the
// archive leg that never answers, not anything the storm did to it.
void TestIntegration::pagingPastTheOldestStoredMessageNeverAnswers() {
    TackyBackend backend;
    QVERIFY(backend.start());
    addAccount(backend, "me@example.com");

    ChatModel chat;
    chat.setBackend(&backend);
    chat.setAccount("me@example.com");
    chat.setChat("friend@example.com");

    for (int i = 0; i < 20; ++i)
        backend.notify("message", "send",
                       QVariantMap{{"acc", "me@example.com"},
                                   {"chat", "friend@example.com"},
                                   {"body", QStringLiteral("m%1").arg(i)}});
    QTRY_VERIFY_WITH_TIMEOUT(chat.rowCount() == 20, 10000);

    const qlonglong oldest =
        chat.data(chat.index(chat.rowCount() - 1), ChatModel::TimestampRole)
            .toLongLong();

    QSignalSpy results(&backend, &TackyBackend::result);
    QSignalSpy errors(&backend, &TackyBackend::error);
    const int tok = backend.request(
        "message", "history",
        QVariantMap{{"acc", "me@example.com"},
                    {"chat", "friend@example.com"},
                    {"limit", 50},
                    {"before", oldest},
                    {"tag", "probe"}});
    QTest::qWait(2000);
    QVERIFY2(resultFor(results, tok).isValid() || !errors.isEmpty(),
             "the page below the oldest stored message got neither a result "
             "nor an error: it is out for good, and the direction that asked "
             "for it stays latched in ChatModel::m_inflight");

    backend.stop();
}

QTEST_MAIN(TestIntegration)
#include "tst_integration.moc"
