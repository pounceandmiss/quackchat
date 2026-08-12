// NotificationController is a translator, so it is driven here with canned
// tacky JSON the same way the model tests are, against a Notifier that only
// records. What the alert then looks like is the platform notifier's business
// (tst_fdonotifier).
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

#include "NotificationController.h"
#include "Notifier.h"
#include "TackyBackend.h"

namespace {

class RecordingNotifier : public Notifier {
public:
    struct Shown {
        QString key;
        Alert alert;
    };

    QList<Shown> shown;
    QStringList retracted;
    int retractAllCount = 0;

    void show(const QString &key, const Alert &alert) override {
        shown.append({key, alert});
    }
    void retract(const QString &key) override { retracted.append(key); }
    void retractAll() override { ++retractAllCount; }
};

// The separator NotificationController keys chats by; a JID cannot contain it.
QString key(const QString &acc, const QString &jid) {
    return acc + QChar(0x1f) + jid;
}

} // namespace

class TestNotifications : public QObject {
    Q_OBJECT

private:
    NotificationController *m_controller = nullptr;
    RecordingNotifier *m_notifier = nullptr;

    // Decode an ["event",module,name,args] frame and route it in exactly as
    // TackyBackend would.
    void feed(const QByteArray &json) {
        const QJsonArray a = QJsonDocument::fromJson(json).array();
        m_controller->handleEvent(a.at(1).toString(), a.at(2).toString(),
                                  a.at(3).toVariant());
    }

private slots:
    void init() {
        m_controller = new NotificationController(this);
        m_notifier = new RecordingNotifier;
        m_controller->setNotifier(m_notifier); // takes ownership
    }

    void cleanup() {
        delete m_controller;
        m_controller = nullptr;
        m_notifier = nullptr; // died with the controller
    }

    void notifyBecomesAnAlert() {
        feed(R"(["event","notify","<Notify>",{"acc":"me@h","jid":"ann@h",
              "nick":"Ann","body":"pub?","timestamp":1700000000,"unread":3,
              "mention":false}])");

        QCOMPARE(m_notifier->shown.size(), 1);
        const auto &s = m_notifier->shown.at(0);
        QCOMPARE(s.key, key("me@h", "ann@h"));
        QCOMPARE(s.alert.nick, QStringLiteral("Ann"));
        QCOMPARE(s.alert.body, QStringLiteral("pub?"));
        QCOMPARE(s.alert.unread, 3);
        QCOMPARE(s.alert.mention, false);
    }

    void mentionCarriesThrough() {
        feed(R"(["event","notify","<Notify>",{"acc":"me@h","jid":"room@c",
              "nick":"Bo","body":"you about?","unread":1,"mention":true}])");

        QCOMPARE(m_notifier->shown.size(), 1);
        QCOMPARE(m_notifier->shown.at(0).alert.mention, true);
    }

    // An alert naming the JID beats one with a blank sender. Same fallback
    // Android's Notifications.java applies.
    void blankNickFallsBackToJid() {
        feed(R"(["event","notify","<Notify>",{"acc":"me@h","jid":"ann@h",
              "nick":"","body":"hi","unread":1}])");

        QCOMPARE(m_notifier->shown.size(), 1);
        QCOMPARE(m_notifier->shown.at(0).alert.nick, QStringLiteral("ann@h"));
    }

    // The watermark moving is the only thing that takes an alert down, and it
    // arrives whether the chat was read here, in another window or on another
    // device.
    void ownReadRetracts() {
        feed(R"(["event","message","<OwnRead>",{"acc":"me@h","jid":"ann@h",
              "timestamp":1700000000}])");

        QCOMPARE(m_notifier->retracted, QStringList{key("me@h", "ann@h")});
        QVERIFY(m_notifier->shown.isEmpty());
    }

    // Two accounts talking to the same JID are two separate alerts.
    void accountIsPartOfTheKey() {
        feed(R"(["event","notify","<Notify>",{"acc":"one@h","jid":"ann@h",
              "nick":"Ann","body":"a","unread":1}])");
        feed(R"(["event","notify","<Notify>",{"acc":"two@h","jid":"ann@h",
              "nick":"Ann","body":"b","unread":1}])");

        QCOMPARE(m_notifier->shown.size(), 2);
        QVERIFY(m_notifier->shown.at(0).key != m_notifier->shown.at(1).key);
    }

    void otherEventsAreIgnored() {
        feed(R"(["event","message","<New>",{"acc":"me@h","jid":"ann@h"}])");
        feed(R"(["event","notify","<Settings>",{"acc":"me@h","jid":"ann@h",
              "muted":true,"mentions":false}])");

        QVERIFY(m_notifier->shown.isEmpty());
        QVERIFY(m_notifier->retracted.isEmpty());
    }

    void incompleteEventsAreIgnored() {
        feed(R"(["event","notify","<Notify>",{"jid":"ann@h","nick":"Ann"}])");
        feed(R"(["event","notify","<Notify>",{"acc":"me@h","nick":"Ann"}])");
        feed(R"(["event","message","<OwnRead>",{"acc":"me@h"}])");

        QVERIFY(m_notifier->shown.isEmpty());
        QVERIFY(m_notifier->retracted.isEmpty());
    }

    // The chat the user picked, split back out of the key.
    void activationNamesTheChat() {
        QSignalSpy spy(m_controller, &NotificationController::activated);
        emit m_notifier->activated(key("me@h", "ann@h"));

        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("me@h"));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("ann@h"));
    }

    // Nothing is arriving to retract these the ordinary way any more, so what
    // is on screen would outlive the session it belongs to.
    void backendStoppingClearsEverything() {
        TackyBackend backend;
        m_controller->setBackend(&backend);
        QVERIFY(!backend.isRunning());

        emit backend.runningChanged();

        QCOMPARE(m_notifier->retractAllCount, 1);
    }

    // A platform with no implementation: every event still has to be safe.
    void noNotifierIsHarmless() {
        m_controller->setNotifier(nullptr);

        feed(R"(["event","notify","<Notify>",{"acc":"me@h","jid":"ann@h",
              "nick":"Ann","body":"hi","unread":1}])");
        feed(R"(["event","message","<OwnRead>",{"acc":"me@h","jid":"ann@h"}])");

        QVERIFY(m_controller->notifier() == nullptr);
    }
};

QTEST_MAIN(TestNotifications)
#include "tst_notifications.moc"
