// FdoNotifier against a stand-in notification daemon on a private session bus.
// CMake runs this under `dbus-run-session`, which is what lets the fake own
// org.freedesktop.Notifications - on the real bus the desktop's own daemon
// already has that name.
//
// The async bookkeeping is what is worth testing here: every call to the bus
// is a round trip, and a chat's alerts can outrun it.
#include <QDBusConnection>
#include <QSignalSpy>
#include <QtTest>

#include "FdoNotifier.h"

namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";

class FakeDaemon : public QObject {
    Q_OBJECT
    // Without this the slots below are exported as `local.FakeDaemon` and a
    // call addressed to the notification interface answers "no such
    // interface".
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    struct Posted {
        QString app;
        uint replaces;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
    };

    QList<Posted> posted;
    QList<uint> closed;
    QStringList caps;

    // Ids restart too, so each test can talk about notification 1.
    void reset() {
        posted.clear();
        closed.clear();
        caps = QStringList{QStringLiteral("body"), QStringLiteral("actions"),
                           QStringLiteral("body-markup")};
        m_nextId = 1;
    }

public slots:
    QStringList GetCapabilities() { return caps; }

    uint Notify(const QString &app, uint replaces, const QString &icon,
                const QString &summary, const QString &body,
                const QStringList &actions, const QVariantMap &hints,
                int timeout) {
        Q_UNUSED(icon)
        Q_UNUSED(timeout)
        posted.append({app, replaces, summary, body, actions, hints});
        // Per spec a replacement keeps the id it replaced.
        return replaces ? replaces : m_nextId++;
    }

    void CloseNotification(uint id) { closed.append(id); }

signals:
    void ActionInvoked(uint id, const QString &action);
    void NotificationClosed(uint id, uint reason);

private:
    uint m_nextId = 1;
};

Notifier::Alert alertOf(const QString &nick, const QString &body,
                        int unread = 1) {
    Notifier::Alert a;
    a.nick = nick;
    a.body = body;
    a.unread = unread;
    return a;
}

} // namespace

class TestFdoNotifier : public QObject {
    Q_OBJECT

private:
    FakeDaemon *m_daemon = nullptr;
    // One per test, so a leftover from the last one is not still listening to
    // the bus signals this one raises.
    FdoNotifier *m_notifier = nullptr;

    // A notifier that has finished negotiating capabilities, so tests start
    // from the steady state rather than the startup race.
    FdoNotifier *readyNotifier() {
        m_notifier = new FdoNotifier;
        if (!QTest::qWaitFor([this] { return m_notifier->isAvailable(); }, 5000))
            qWarning("the stand-in daemon never answered GetCapabilities");
        return m_notifier;
    }

private slots:
    void initTestCase() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY2(bus.isConnected(), "no session bus; run under dbus-run-session");

        m_daemon = new FakeDaemon;
        QVERIFY(bus.registerObject(kPath, m_daemon,
                                   QDBusConnection::ExportAllSlots
                                       | QDBusConnection::ExportAllSignals));
        QVERIFY2(bus.registerService(kService),
                 "another daemon owns the name; this needs a private bus");
    }

    void cleanupTestCase() {
        QDBusConnection::sessionBus().unregisterObject(kPath);
        delete m_daemon;
    }

    void init() { m_daemon->reset(); }

    void cleanup() {
        delete m_notifier;
        m_notifier = nullptr;
    }

    void postsTheAlert() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "pub?"));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        const auto &p = m_daemon->posted.at(0);
        QCOMPARE(p.summary, QStringLiteral("Ann"));
        QCOMPARE(p.body, QStringLiteral("pub?"));
        QCOMPARE(p.replaces, 0u); // nothing to replace yet
        QCOMPARE(p.hints.value(QStringLiteral("category")).toString(),
                 QStringLiteral("im.received"));
        // The key for "the popup itself was clicked".
        QVERIFY(p.actions.contains(QStringLiteral("default")));
    }

    // `unread` is the chat's whole count, so a burst reads as one alert that
    // grew - the shape Android's Notifications.java builds too.
    void summaryCountsUnread() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "hi", 12));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        QCOMPARE(m_daemon->posted.at(0).summary, QStringLiteral("Ann (12)"));
    }

    // One alert per chat, rewritten in place rather than stacked on.
    void secondMessageRewritesTheFirst() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1);

        n->show(QStringLiteral("k1"), alertOf("Ann", "two", 2));
        QTRY_COMPARE(m_daemon->posted.size(), 2);
        QCOMPARE(m_daemon->posted.at(1).replaces, 1u);
    }

    // The case the id bookkeeping exists for: a catch-up burst for one chat
    // outruns the bus. Without it the later alerts carry replaces_id=0 and
    // stack up as separate popups.
    void burstStillRewritesInPlace() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        n->show(QStringLiteral("k1"), alertOf("Ann", "two", 2));
        n->show(QStringLiteral("k1"), alertOf("Ann", "three", 3));

        QTRY_COMPARE(m_daemon->posted.size(), 2);
        // Only the newest of what piled up behind the in-flight call is sent.
        QCOMPARE(m_daemon->posted.at(1).summary, QStringLiteral("Ann (3)"));
        QCOMPARE(m_daemon->posted.at(1).replaces, 1u);
    }

    void differentChatsAreDifferentAlerts() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1);
        n->show(QStringLiteral("k2"), alertOf("Bo", "two"));

        QTRY_COMPARE(m_daemon->posted.size(), 2);
        QCOMPARE(m_daemon->posted.at(1).replaces, 0u);
    }

    void retractClosesIt() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1);

        n->retract(QStringLiteral("k1"));
        QTRY_COMPARE(m_daemon->closed, QList<uint>{1u});
    }

    // The read watermark can move before the alert even has an id - marking a
    // chat read is a lot faster than a bus round trip.
    void retractBeforeTheIdArrivesStillCloses() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        n->retract(QStringLiteral("k1"));

        QTRY_COMPARE(m_daemon->closed, QList<uint>{1u});
        QCOMPARE(m_daemon->posted.size(), 1);
    }

    void retractAllClosesEverything() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        n->show(QStringLiteral("k2"), alertOf("Bo", "two"));
        QTRY_COMPARE(m_daemon->posted.size(), 2);

        n->retractAll();
        QTRY_COMPARE(m_daemon->closed.size(), 2);
    }

    // A message body is arbitrary text, and a daemon that parses markup would
    // otherwise swallow the rest of the line after a "<".
    void bodyIsEscapedForMarkupDaemons() {
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "a < b & c"));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        QCOMPARE(m_daemon->posted.at(0).body,
                 QStringLiteral("a &lt; b &amp; c"));
    }

    // ... and a daemon that does not would show the entities to the user.
    void bodyIsLeftAloneOtherwise() {
        m_daemon->caps = QStringList{QStringLiteral("body")};
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "a < b & c"));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        QCOMPARE(m_daemon->posted.at(0).body, QStringLiteral("a < b & c"));
    }

    void noActionsWhenTheDaemonHasNone() {
        m_daemon->caps = QStringList{QStringLiteral("body")};
        FdoNotifier *n = readyNotifier();
        n->show(QStringLiteral("k1"), alertOf("Ann", "hi"));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        QVERIFY(m_daemon->posted.at(0).actions.isEmpty());
    }

    void clickNamesTheChat() {
        FdoNotifier *n = readyNotifier();
        QSignalSpy spy(n, &Notifier::activated);
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1);

        emit m_daemon->ActionInvoked(1u, QStringLiteral("default"));

        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("k1"));
    }

    // ActionInvoked is broadcast to everyone on the bus, so most of what
    // arrives belongs to some other app.
    void someoneElsesNotificationIsIgnored() {
        FdoNotifier *n = readyNotifier();
        QSignalSpy spy(n, &Notifier::activated);
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1);

        emit m_daemon->ActionInvoked(9999u, QStringLiteral("default"));
        emit m_daemon->ActionInvoked(1u, QStringLiteral("something-else"));
        // Delivery on one connection is in order, so the two above have been
        // seen and discarded by the time this one gets through.
        emit m_daemon->ActionInvoked(1u, QStringLiteral("default"));

        QTRY_COMPARE(spy.size(), 1);
    }

    // Dismissed by hand: the id is dead, so the next message for that chat
    // starts a new alert rather than trying to replace a stale one.
    void dismissedAlertIsNotReplaced() {
        FdoNotifier *n = readyNotifier();
        QSignalSpy spy(n, &Notifier::activated);
        n->show(QStringLiteral("k1"), alertOf("Ann", "one"));
        QTRY_COMPARE(m_daemon->posted.size(), 1); // id 1
        n->show(QStringLiteral("k2"), alertOf("Bo", "hi"));
        QTRY_COMPARE(m_daemon->posted.size(), 2); // id 2

        emit m_daemon->NotificationClosed(1u, 2u); // 2 = dismissed by the user
        // Signals reach the notifier in the order they were sent, so k2 being
        // acted on means k1's dismissal already has been. A reply to a call
        // would not do: that travels a different path and can overtake.
        emit m_daemon->ActionInvoked(2u, QStringLiteral("default"));
        QTRY_COMPARE(spy.size(), 1);

        n->show(QStringLiteral("k1"), alertOf("Ann", "two", 2));
        QTRY_COMPARE(m_daemon->posted.size(), 3);
        QCOMPARE(m_daemon->posted.at(2).replaces, 0u);
    }

    // Alerts raised before the daemon has answered are held, not dropped and
    // not sent blind - until then we do not know how the body will be parsed.
    void alertsBeforeCapabilitiesAreHeld() {
        m_notifier = new FdoNotifier;
        QVERIFY(!m_notifier->isAvailable());
        m_notifier->show(QStringLiteral("k1"), alertOf("Ann", "a < b"));

        QTRY_COMPARE(m_daemon->posted.size(), 1);
        QCOMPARE(m_daemon->posted.at(0).body, QStringLiteral("a &lt; b"));
    }
};

QTEST_MAIN(TestFdoNotifier)
#include "tst_fdonotifier.moc"
