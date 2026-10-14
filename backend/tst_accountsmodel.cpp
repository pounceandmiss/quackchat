#include <QtTest>
#include <QSignalSpy>

#include "AccountsModel.h"
#include "TackyBackend.h"

class TestAccountsModel : public QObject {
    Q_OBJECT

    static QString jidAt(const AccountsModel &m, int row) {
        return m.data(m.index(row), AccountsModel::JidRole).toString();
    }
    static QString connAt(const AccountsModel &m, int row) {
        return m.data(m.index(row), AccountsModel::ConnStateRole).toString();
    }
    static bool enabledAt(const AccountsModel &m, int row) {
        return m.data(m.index(row), AccountsModel::EnabledRole).toBool();
    }
    static bool knownAt(const AccountsModel &m, int row) {
        return m.data(m.index(row), AccountsModel::StatusKnownRole).toBool();
    }

private slots:
    void listSortsAndMarksEnabled();
    void enabledBeforeList();
    void addedRemovedEvents();
    void enableDisableEvents();
    void enableCreatesMissingRow();
    void connStateEvents();
    void connErrorOutlivesTheRetry();
    void proxyFollowsState();
    void lookups();
    void integrationListsAddedAccount();
    void integrationDeadProxyIsReported();
    void integrationEnableRetriesNow();
    void pullsConnStateForRowsFromTheList();
    void addSignsInOnTheRouteGiven();
    void statusIsUnknownUntilBothRepliesLand();
    void aDisabledAccountNeedsNoConnState();
    void statusKnownBumpsConnRev();
};

// The three replies that make up a row arrive separately, and the rail draws
// whatever it has: "offline"/"disabled" in that window is a claim about an
// account that may well be connected.
void TestAccountsModel::statusIsUnknownUntilBothRepliesLand() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    QVERIFY(!knownAt(m, 0));

    m.applyEnabledList(QVariantList{"amy@h"});
    QVERIFY2(!knownAt(m, 0), "enabled alone says nothing about the connection");

    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "connected"}});
    QVERIFY(knownAt(m, 0));
    QVERIFY(m.statusKnownFor("amy@h"));
    // Nothing is known about an account with no row at all.
    QVERIFY(!m.statusKnownFor("nobody@h"));
}

// A disabled account never gets a conn event, so waiting for one would leave it
// indeterminate for good.
void TestAccountsModel::aDisabledAccountNeedsNoConnState() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h", "bob@h"});
    m.applyEnabledList(QVariantList{"amy@h"});
    QVERIFY(knownAt(m, 1)); // bob@h, absent from the enabled subset
    QVERIFY(!knownAt(m, 0));

    // The same the other way round: the enabled subset can land first.
    AccountsModel n;
    n.applyEnabledList(QVariantList{"amy@h"});
    n.applyList(QVariantList{"amy@h", "bob@h"});
    QVERIFY(knownAt(n, 1));
    QVERIFY(!knownAt(n, 0));
}

// statusKnownFor() is a call, so the header badge's binding re-runs off connRev
// or not at all.
void TestAccountsModel::statusKnownBumpsConnRev() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    QSignalSpy rev(&m, &AccountsModel::connRevChanged);
    m.applyEnabledList(QVariantList{"amy@h"});
    QCOMPARE(rev.count(), 1);
    // Same value as the row was built with, but now it is a fact.
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", ""}});
    QCOMPARE(rev.count(), 2);
    QVERIFY(knownAt(m, 0));
}

// conn state only ever arrives as an event. Attaching to a backend whose
// accounts are already online means those events are long past, so the rows
// have to ask for the current value or they sit on the default forever.
void TestAccountsModel::pullsConnStateForRowsFromTheList() {
    TackyBackend backend;
    AccountsModel m;
    m.setBackend(&backend);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.applyList(QVariantList{"a@h", "b@h"});

    QStringList pulled;
    for (const QList<QVariant> &call : sent) {
        if (call.at(0).toString() != "conn" || call.at(1).toString() != "pull")
            continue;
        const QVariantMap a = call.at(2).toMap();
        pulled << a.value("acc").toString() + "/" + a.value("event").toString();
    }
    QVERIFY(pulled.contains("a@h/State"));
    QVERIFY(pulled.contains("b@h/State"));
}

// The route rides on `account add`, ahead of the enable that signs in, so the
// first connection already takes it.
void TestAccountsModel::addSignsInOnTheRouteGiven() {
    TackyBackend backend;
    AccountsModel m;
    m.setBackend(&backend);

    QSignalSpy sent(&backend, &TackyBackend::sent);
    m.add("a@h", "pw", "", true);
    QCOMPARE(sent.size(), 2);
    QCOMPARE(sent.at(0).at(1).toString(), QString("add"));
    const QVariantMap a = sent.at(0).at(2).toMap();
    QCOMPARE(a.value("proxy").toString(), QString());
    QVERIFY(a.value("tor").toBool());
    QCOMPARE(sent.at(1).at(1).toString(), QString("enable"));
}

void TestAccountsModel::listSortsAndMarksEnabled() {
    AccountsModel m;
    m.applyList(QVariantList{"bob@h", "amy@h"});
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(jidAt(m, 0), QString("amy@h")); // sorted by JID
    QCOMPARE(jidAt(m, 1), QString("bob@h"));
    QVERIFY(!enabledAt(m, 0));

    m.applyEnabledList(QVariantList{"amy@h"});
    QVERIFY(enabledAt(m, 0));
    QVERIFY(!enabledAt(m, 1));
}

// The enabled subset can land before the full list; the flag must still stick.
void TestAccountsModel::enabledBeforeList() {
    AccountsModel m;
    m.applyEnabledList(QVariantList{"amy@h"});
    m.applyList(QVariantList{"amy@h", "bob@h"});
    QVERIFY(enabledAt(m, 0));
    QVERIFY(!enabledAt(m, 1));
}

void TestAccountsModel::addedRemovedEvents() {
    AccountsModel m;
    QSignalSpy cnt(&m, &AccountsModel::countChanged);
    m.handleEvent("account", "Added", QVariantMap{{"acc", "amy@h"}});
    m.handleEvent("account", "Added", QVariantMap{{"acc", "bob@h"}});
    QCOMPARE(m.rowCount(), 2);
    m.handleEvent("account", "Added", QVariantMap{{"acc", "amy@h"}}); // dup no-op
    QCOMPARE(m.rowCount(), 2);
    m.handleEvent("account", "Removed", QVariantMap{{"acc", "amy@h"}});
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(jidAt(m, 0), QString("bob@h"));
    QVERIFY(cnt.count() >= 3);
}

void TestAccountsModel::enableDisableEvents() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    // isEnabled() is a call, so a QML binding on it re-runs off connRev or not
    // at all - the header badge would sit on a colour that stopped being true.
    QSignalSpy rev(&m, &AccountsModel::connRevChanged);
    QVERIFY(!enabledAt(m, 0));
    m.handleEvent("account", "Enabled", QVariantMap{{"acc", "amy@h"}});
    QVERIFY(enabledAt(m, 0));
    QCOMPARE(rev.count(), 1);
    m.handleEvent("account", "Disabled", QVariantMap{{"acc", "amy@h"}});
    QVERIFY(!enabledAt(m, 0));
    QCOMPARE(rev.count(), 2);
    m.applyEnabledList(QVariantList{"amy@h"});
    QVERIFY(enabledAt(m, 0));
    QCOMPARE(rev.count(), 3);
}

// An <Enabled> for an account not yet listed still surfaces it (fresh add race).
void TestAccountsModel::enableCreatesMissingRow() {
    AccountsModel m;
    m.handleEvent("account", "Enabled", QVariantMap{{"acc", "new@h"}});
    QCOMPARE(m.rowCount(), 1);
    QVERIFY(enabledAt(m, 0));
}

void TestAccountsModel::connStateEvents() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "connecting"}});
    QCOMPARE(connAt(m, 0), QString("connecting"));
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "connected"}});
    QCOMPARE(connAt(m, 0), QString("connected"));
    m.handleEvent("conn", "AuthError", QVariantMap{{"acc", "amy@h"}});
    QCOMPARE(connAt(m, 0), QString("auth-error"));
    m.handleEvent("conn", "ConnError", QVariantMap{{"acc", "amy@h"}});
    QCOMPARE(connAt(m, 0), QString("conn-error"));

    // a conn event for an unlisted account surfaces it too
    m.handleEvent("conn", "State", QVariantMap{{"acc", "z@h"}, {"state", "waiting"}});
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.connStateFor("z@h"), QString("waiting"));
}

// The route comes with every <State>, so whichever arrives last says it: one
// through a proxy names it, and one without (direct, or disconnected) clears it.
void TestAccountsModel::proxyFollowsState() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    QVERIFY(m.proxyFor("amy@h").isEmpty());
    const int rev = m.connRev();
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"},
                                               {"state", "connected"},
                                               {"proxy", "127.0.0.1:1080"}});
    QCOMPARE(m.proxyFor("amy@h"), QString("127.0.0.1:1080"));
    QCOMPARE(m.data(m.index(0), AccountsModel::ProxyRole).toString(),
             QString("127.0.0.1:1080"));
    QVERIFY(m.connRev() > rev);
    // A failure leaves the route as it was: the <State> after it says the next.
    m.handleEvent("conn", "ConnError", QVariantMap{{"acc", "amy@h"}, {"message", "x"}});
    QCOMPARE(m.proxyFor("amy@h"), QString("127.0.0.1:1080"));
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"},
                                               {"state", "disconnected"},
                                               {"proxy", ""}});
    QVERIFY(m.proxyFor("amy@h").isEmpty());

    // An unlisted account's first <State> lands its route on the row it adds.
    m.handleEvent("conn", "State", QVariantMap{{"acc", "z@h"},
                                               {"state", "connecting"},
                                               {"proxy", "[::1]:1080"}});
    QCOMPARE(m.proxyFor("z@h"), QString("[::1]:1080"));
}

// The reason is what matters while tacky retries, so the `waiting` and
// `connecting` after it leave it standing; getting through clears it.
void TestAccountsModel::connErrorOutlivesTheRetry() {
    AccountsModel m;
    m.applyList(QVariantList{"amy@h"});
    QSignalSpy rev(&m, &AccountsModel::connRevChanged);
    m.handleEvent("conn", "ConnError",
                  QVariantMap{{"acc", "amy@h"}, {"message", "Connect failed: proxy: refused"}});
    QCOMPARE(m.connErrorFor("amy@h"), QString("Connect failed: proxy: refused"));
    QCOMPARE(m.data(m.index(0), AccountsModel::ConnErrorRole).toString(),
             QString("Connect failed: proxy: refused"));
    QVERIFY(rev.count() > 0);

    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "waiting"}});
    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "connecting"}});
    QCOMPARE(m.connErrorFor("amy@h"), QString("Connect failed: proxy: refused"));

    m.handleEvent("conn", "State", QVariantMap{{"acc", "amy@h"}, {"state", "connected"}});
    QVERIFY(m.connErrorFor("amy@h").isEmpty());

    m.handleEvent("conn", "AuthError",
                  QVariantMap{{"acc", "amy@h"}, {"message", "not-authorized"}});
    QCOMPARE(m.connErrorFor("amy@h"), QString("not-authorized"));
    // Turned off on purpose: nothing to report.
    m.handleEvent("account", "Disabled", QVariantMap{{"acc", "amy@h"}});
    QVERIFY(m.connErrorFor("amy@h").isEmpty());

    // tacky giving up says why, and leaves the account offline.
    m.handleEvent("conn", "Disconnected",
                  QVariantMap{{"acc", "new@h"}, {"message", "gave up"}});
    QCOMPARE(m.connStateFor("new@h"), QString("disconnected"));
    QCOMPARE(m.connErrorFor("new@h"), QString("gave up"));
}

void TestAccountsModel::lookups() {
    AccountsModel m;
    m.applyList(QVariantList{"bob@h", "amy@h"});
    m.applyEnabledList(QVariantList{"amy@h"});
    QCOMPARE(m.firstJid(), QString("amy@h"));
    QVERIFY(m.contains("bob@h"));
    QVERIFY(!m.contains("nobody@h"));
    QVERIFY(m.isEnabled("amy@h"));
    QVERIFY(!m.isEnabled("bob@h"));
    QCOMPARE(m.connStateFor("nobody@h"), QString());
}

// Real backend: a proxy nobody listens on fails the connection, and the model
// keeps tacky's reason through the retries that follow, rather than showing
// only "reconnecting".
void TestAccountsModel::integrationDeadProxyIsReported() {
    TackyBackend backend;
    QVERIFY(backend.start());
    AccountsModel m;
    m.setBackend(&backend);
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"proxy", "socks5://127.0.0.1:1"}});
    backend.notify("account", "enable", QVariantMap{{"acc", "me@example.com"}});

    QTRY_VERIFY_WITH_TIMEOUT(!m.connErrorFor("me@example.com").isEmpty(), 10000);
    // Said once, by tacky: "Connect failed: can't reach the proxy: <why>".
    QVERIFY2(m.connErrorFor("me@example.com")
                 .startsWith("Connect failed: can't reach the proxy: "),
             qPrintable(m.connErrorFor("me@example.com")));
    QCOMPARE(m.connErrorFor("me@example.com").count("proxy"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(m.connStateFor("me@example.com"), QString("waiting"), 5000);
    QVERIFY(!m.connErrorFor("me@example.com").isEmpty());
    // Still on its way through the proxy, which <State> names without the scheme.
    QCOMPARE(m.proxyFor("me@example.com"), QString("127.0.0.1:1"));

    backend.stop();
}

// Real backend: `enable` on an enabled account that is waiting out its backoff
// tries again at once, which is what the account page's "Try again now" is.
void TestAccountsModel::integrationEnableRetriesNow() {
    TackyBackend backend;
    QVERIFY(backend.start());
    AccountsModel m;
    m.setBackend(&backend);
    QSignalSpy events(&backend, &TackyBackend::event);
    const auto attempts = [&] {
        int n = 0;
        for (const auto &e : events)
            if (e.at(0).toString() == "conn" && e.at(1).toString() == "State" &&
                e.at(2).toMap().value("state").toString() == "connecting")
                ++n;
        return n;
    };
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"proxy", "socks5://127.0.0.1:1"}});
    backend.notify("account", "enable", QVariantMap{{"acc", "me@example.com"}});

    // Three failures in (after waits of 1s and 2s), the next wait is 5s.
    QTRY_VERIFY_WITH_TIMEOUT(attempts() >= 3, 8000);
    QTRY_COMPARE_WITH_TIMEOUT(m.connStateFor("me@example.com"), QString("waiting"), 2000);
    const int before = attempts();
    m.enable("me@example.com");
    QTRY_VERIFY_WITH_TIMEOUT(attempts() > before, 1000);

    backend.stop();
}

// Real backend: adding + enabling an account surfaces it through the model's
// refresh/event path.
void TestAccountsModel::integrationListsAddedAccount() {
    TackyBackend backend;
    QVERIFY(backend.start());

    AccountsModel m;
    m.setBackend(&backend);

    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});
    backend.notify("account", "enable",
                   QVariantMap{{"acc", "me@example.com"}});

    QTRY_VERIFY_WITH_TIMEOUT(m.rowCount() == 1, 5000);
    QCOMPARE(jidAt(m, 0), QString("me@example.com"));
    QTRY_VERIFY_WITH_TIMEOUT(m.isEnabled("me@example.com"), 5000);

    backend.stop();
}

QTEST_MAIN(TestAccountsModel)
#include "tst_accountsmodel.moc"
