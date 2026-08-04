// Against a REAL libtacky backend (no server needed). `message send` stores
// locally and emits real message/<New> events, exercising the full delivery
// path: backend emit -> queued hop -> TackyBackend signal -> model.
#include <QtTest>
#include <QSignalSpy>

#include "ChatListModel.h"
#include "ChatModel.h"
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

QTEST_MAIN(TestIntegration)
#include "tst_integration.moc"
