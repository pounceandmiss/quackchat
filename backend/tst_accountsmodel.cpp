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

private slots:
    void listSortsAndMarksEnabled();
    void enabledBeforeList();
    void addedRemovedEvents();
    void enableDisableEvents();
    void enableCreatesMissingRow();
    void connStateEvents();
    void lookups();
    void integrationListsAddedAccount();
    void pullsConnStateForRowsFromTheList();
};

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
    m.handleEvent("conn", "Ready", QVariantMap{{"acc", "amy@h"}});
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
