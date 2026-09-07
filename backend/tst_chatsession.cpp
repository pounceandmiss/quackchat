// What the composer is doing, which the session owns rather than the view: a
// reply and an edit are exclusive, and an edit borrows the field the draft had.
#include <QtTest>
#include <QSignalSpy>

#include "ChatSession.h"
#include "TackyBackend.h"

class TestChatSession : public QObject {
    Q_OBJECT
private slots:
    void sendingWhileEditingCorrectsInstead();
    void cancellingAnEditGivesTheDraftBack();
    void sendingAnEditGivesTheDraftBackToo();
    void aSecondEditKeepsTheFirstStash();
    void replyAndEditTurnEachOtherOff();
};

// The args of the last `message <method>` the spy saw, empty when there was
// none. By method, since setting the chat issues a history call over the same
// module.
static QVariantMap lastArgs(const QSignalSpy &spy, const char *method) {
    QVariantMap out;
    for (const QList<QVariant> &call : spy)
        if (call.at(1).toString() == QLatin1String(method))
            out = call.at(2).toMap();
    return out;
}

void TestChatSession::sendingWhileEditingCorrectsInstead() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.editMessage(300, "teh cat");
    QVERIFY(s.editing());
    QCOMPARE(s.draft(), QString("teh cat"));

    s.setDraft("the cat");
    s.sendDraft();

    const QVariantMap args = lastArgs(sent, "edit");
    QCOMPARE(args.value("timestamp").toLongLong(), 300LL);
    QCOMPARE(args.value("body").toString(), QString("the cat"));
    // A correction instead of a send, not as well as one.
    QVERIFY(lastArgs(sent, "send").isEmpty());
    // And the composer is out of edit mode, not still armed at that row.
    QVERIFY(!s.editing());
}

void TestChatSession::cancellingAnEditGivesTheDraftBack() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);
    QSignalSpy edit(&s, &ChatSession::editChanged);

    s.setDraft("half a sentence");
    s.editMessage(300, "teh cat");
    QCOMPARE(s.draft(), QString("teh cat"));
    QCOMPARE(edit.count(), 1);

    s.cancelEdit();
    QVERIFY(!s.editing());
    QCOMPARE(s.draft(), QString("half a sentence"));
    QCOMPARE(edit.count(), 2);
}

void TestChatSession::sendingAnEditGivesTheDraftBackToo() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);

    s.setDraft("half a sentence");
    s.editMessage(300, "teh cat");
    s.setDraft("the cat");
    s.sendDraft();

    QVERIFY(!s.editing());
    QCOMPARE(s.draft(), QString("half a sentence"));
}

void TestChatSession::aSecondEditKeepsTheFirstStash() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);

    s.setDraft("half a sentence");
    s.editMessage(300, "first");
    // Straight from one edit to another without leaving: what is in the field
    // now belongs to the first edit, and is not what was being written.
    s.editMessage(400, "second");
    QCOMPARE(s.draft(), QString("second"));

    s.cancelEdit();
    QCOMPARE(s.draft(), QString("half a sentence"));
}

void TestChatSession::replyAndEditTurnEachOtherOff() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);

    s.replyToMessage(300, "the question", false);
    QVERIFY(s.replying());
    s.editMessage(400, "teh cat");
    QVERIFY(s.editing());
    QVERIFY(!s.replying());

    s.replyToMessage(300, "the question", false);
    QVERIFY(s.replying());
    QVERIFY(!s.editing());
    // The reply cancelled the edit, so the draft the edit took is back.
    QCOMPARE(s.draft(), QString());
}

QTEST_MAIN(TestChatSession)
#include "tst_chatsession.moc"
