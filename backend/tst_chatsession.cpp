// The composer's state machine, which the session owns rather than the view:
// the draft, the message being answered, and the message being corrected. The
// last two are exclusive, and an edit borrows the field the draft was in.
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

// What went out, as (method, args) for the message module only.
static QList<QVariant> lastCall(const QSignalSpy &spy) {
    for (int i = spy.count() - 1; i >= 0; --i)
        if (spy.at(i).at(0).toString() == QLatin1String("message"))
            return spy.at(i);
    return {};
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

    const QList<QVariant> call = lastCall(sent);
    QCOMPARE(call.at(1).toString(), QString("edit"));
    QCOMPARE(call.at(2).toMap().value("timestamp").toLongLong(), 300LL);
    QCOMPARE(call.at(2).toMap().value("body").toString(), QString("the cat"));
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
    QVERIFY(!s.replying()); // answering the message being corrected makes no sense

    s.replyToMessage(300, "the question", false);
    QVERIFY(s.replying());
    QVERIFY(!s.editing());
    // The reply cancelled the edit, so the draft the edit took is back.
    QCOMPARE(s.draft(), QString());
}

QTEST_MAIN(TestChatSession)
#include "tst_chatsession.moc"
