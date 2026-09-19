// What the composer is doing, which the session owns rather than the view: a
// reply and an edit are exclusive, an edit borrows the field the draft had,
// and the files queued in the tray wait there for the send.
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

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
    void theQueueGoesOutAheadOfTheWords();
    void anEditGoesAloneAndLeavesTheQueueWhereItIs();
    void nothingUnreadableIsEverQueued();
};

// Every `message <method>` the spy saw, as "method arg" pairs. A queued send
// is judged on what went out and in what order, so both are kept.
static QStringList outgoing(const QSignalSpy &spy) {
    QStringList out;
    for (const QList<QVariant> &call : spy) {
        const QString method = call.at(1).toString();
        const QVariantMap args = call.at(2).toMap();
        if (method == QLatin1String("sendFile"))
            out << QStringLiteral("file ") + args.value("path").toString();
        else if (method == QLatin1String("send"))
            out << QStringLiteral("body ") + args.value("body").toString();
    }
    return out;
}

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

// The tray is a queue of messages waiting, not an album: tacky attaches one
// file to a message, so each file goes out as one and the words as another.
void TestChatSession::theQueueGoesOutAheadOfTheWords() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.filePath(QStringLiteral("one.txt"));
    const QString second = dir.filePath(QStringLiteral("two.txt"));
    for (const QString &path : {first, second}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);
    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy queued(&s, &ChatSession::pendingChanged);

    s.attach(QUrl::fromLocalFile(first));
    s.attach(QUrl::fromLocalFile(second));
    QCOMPARE(queued.count(), 2);
    QCOMPARE(s.pending().size(), 2);
    // What the tray draws with.
    const QVariantMap one = s.pending().first().toMap();
    QCOMPARE(one.value("url").toUrl(), QUrl::fromLocalFile(first));
    QCOMPARE(one.value("name").toString(), QStringLiteral("one.txt"));
    QCOMPARE(one.value("isImage").toBool(), false);
    // Queued is not sent.
    QCOMPARE(outgoing(sent), QStringList());

    // Taking one back leaves the rest in the order they were queued in.
    s.attach(QUrl::fromLocalFile(first));
    s.unattach(1);
    QCOMPARE(s.pending().size(), 2);

    s.replyToMessage(300, "the original", false);
    s.setDraft("  and here they are  ");
    s.sendDraft();

    QCOMPARE(outgoing(sent),
             QStringList({QStringLiteral("file ") + first,
                          QStringLiteral("file ") + first,
                          QStringLiteral("body and here they are")}));
    // Everything the send used goes with it, the reply included.
    QVERIFY(s.pending().isEmpty());
    QCOMPARE(s.draft(), QString());
    QVERIFY(!s.replying());
}

// An edit replaces the words of a message already sent, and there is nowhere
// on one to hang a file. The queue waits for a send of its own.
void TestChatSession::anEditGoesAloneAndLeavesTheQueueWhereItIs() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("late.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);
    QSignalSpy sent(&backend, &TackyBackend::sent);

    s.attach(QUrl::fromLocalFile(path));
    s.editMessage(300, "teh cat");
    s.setDraft("the cat");
    s.sendDraft();

    QCOMPARE(lastArgs(sent, "edit").value("body").toString(), QString("the cat"));
    QCOMPARE(outgoing(sent), QStringList());
    QCOMPARE(s.pending().size(), 1);

    // ...and goes out on the next press, with no words needed.
    s.sendDraft();
    QCOMPARE(outgoing(sent), QStringList{QStringLiteral("file ") + path});
    QVERIFY(s.pending().isEmpty());
}

// A url with no file behind it - a dismissed dialog, a link, something moved
// since - leaves the tray as it was rather than queueing a send that cannot
// happen.
void TestChatSession::nothingUnreadableIsEverQueued() {
    TackyBackend backend;
    ChatSession s(&backend, "me@h", "a@h", false);
    QSignalSpy queued(&s, &ChatSession::pendingChanged);

    s.attach(QUrl());
    s.attach(QUrl("https://example.com/cat.png"));
    QVERIFY(s.pending().isEmpty());
    QCOMPARE(queued.count(), 0);

    // Nor does taking back what is not there.
    s.unattach(0);
    s.unattach(-1);
    QCOMPARE(queued.count(), 0);
}
