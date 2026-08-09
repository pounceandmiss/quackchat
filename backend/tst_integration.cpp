// Against a REAL libtacky backend (no server needed). `message send` stores
// locally and emits real message/<New> events, exercising the full delivery
// path: backend emit -> queued hop -> TackyBackend signal -> model.
#include <QtTest>
#include <QSignalSpy>

#include "ChatListModel.h"
#include "ChatModel.h"
#include "MessageMarkup.h"
#include "TackyBackend.h"

namespace {
void addAccount(TackyBackend &b, const QString &acc) {
    b.notify("account", "add",
             QVariantMap{{"acc", acc},
                         {"password", "x"},
                         {"domain", acc.section('@', 1)},
                         {"username", acc.section('@', 0, 0)}});
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

QTEST_MAIN(TestIntegration)
#include "tst_integration.moc"
