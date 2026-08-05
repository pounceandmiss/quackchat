#include <QtTest>
#include <QSignalSpy>

#include "OmemoDevicesModel.h"
#include "TackyBackend.h"

class TestOmemoDevices : public QObject {
    Q_OBJECT

    static QVariant row(int device, const QString &trust, bool active,
                        const QString &fingerprint) {
        return QVariantMap{{"device", device},
                           {"trust", trust},
                           {"active", active},
                           {"fingerprint", fingerprint}};
    }
    static int deviceAt(const OmemoDevicesModel &m, int i) {
        return m.data(m.index(i), OmemoDevicesModel::DeviceRole).toInt();
    }
    static QString trustAt(const OmemoDevicesModel &m, int i) {
        return m.data(m.index(i), OmemoDevicesModel::TrustRole).toString();
    }
    static bool settableAt(const OmemoDevicesModel &m, int i) {
        return m.data(m.index(i), OmemoDevicesModel::SettableRole).toBool();
    }

    // The page's case: the subject is the account itself.
    static void own(OmemoDevicesModel &m, const QString &acc = "me@h") {
        m.setAccount(acc);
        m.setJid(acc);
    }

private slots:
    void excludesOwnDevice();
    void lateOwnDeviceDropsItsRow();
    void trustFlipKeepsRows();
    void deviceComingOrGoingResets();
    void summaryIgnoresCompromised();
    void compromisedIsReadOnly();
    void eventsAreScopedToAccountAndJid();
    void peerListKeepsEveryDevice();
    void integrationEmptyListForFreshAccount();
};

// On the account's own panel the current device is badged separately, so it
// must not turn up among the devices you can trust.
void TestOmemoDevices::excludesOwnDevice() {
    OmemoDevicesModel m;
    own(m);
    m.applyOwnDevice(7);
    m.applyTrustList({row(7, "trusted", true, "aa"), row(8, "undecided", true, "bb")});
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(deviceAt(m, 0), 8);
}

// `device_id` and `trustList` are separate requests, so the list can land while
// we still do not know which device we are.
void TestOmemoDevices::lateOwnDeviceDropsItsRow() {
    OmemoDevicesModel m;
    own(m);
    m.applyTrustList({row(7, "trusted", true, "aa"), row(8, "undecided", true, "bb")});
    QCOMPARE(m.rowCount(), 2);
    m.applyOwnDevice(7);
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(deviceAt(m, 0), 8);
}

void TestOmemoDevices::trustFlipKeepsRows() {
    OmemoDevicesModel m;
    own(m);
    m.applyTrustList({row(8, "undecided", true, "bb"), row(9, "undecided", true, "cc")});
    QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);

    m.applyTrustList({row(8, "trusted", true, "bb"), row(9, "undecided", true, "cc")});
    QCOMPARE(reset.count(), 0); // same devices in the same order
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
    QCOMPARE(trustAt(m, 0), QString("trusted"));
    QCOMPARE(trustAt(m, 1), QString("undecided"));

    // An unchanged list is not worth a signal.
    m.applyTrustList({row(8, "trusted", true, "bb"), row(9, "undecided", true, "cc")});
    QCOMPARE(changed.count(), 1);
}

void TestOmemoDevices::deviceComingOrGoingResets() {
    OmemoDevicesModel m;
    own(m);
    m.applyTrustList({row(8, "undecided", true, "bb")});
    QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
    QSignalSpy count(&m, &OmemoDevicesModel::countChanged);

    m.applyTrustList({row(8, "undecided", true, "bb"), row(9, "undecided", true, "cc")});
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(count.count(), 1);

    m.applyTrustList({row(9, "undecided", true, "cc")});
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(deviceAt(m, 0), 9);
    QCOMPARE(reset.count(), 2);
}

// A compromised row cannot be moved, so it neither counts towards a "set all"
// nor gets a say in whether the others agree.
void TestOmemoDevices::summaryIgnoresCompromised() {
    OmemoDevicesModel m;
    own(m);
    m.applyTrustList({row(8, "trusted", true, "bb"), row(9, "undecided", true, "cc")});
    QCOMPARE(m.settableCount(), 2);
    QCOMPARE(m.commonTrust(), QString()); // they disagree

    m.applyTrustList({row(8, "trusted", true, "bb"), row(9, "trusted", true, "cc")});
    QCOMPARE(m.commonTrust(), QString("trusted"));

    m.applyTrustList({row(8, "trusted", true, "bb"), row(9, "compromised", true, "cc")});
    QCOMPARE(m.settableCount(), 1);
    QCOMPARE(m.commonTrust(), QString("trusted"));

    m.applyTrustList({});
    QCOMPARE(m.settableCount(), 0);
    QCOMPARE(m.commonTrust(), QString());
}

void TestOmemoDevices::compromisedIsReadOnly() {
    OmemoDevicesModel m;
    own(m);
    m.applyTrustList({row(8, "compromised", true, "bb"), row(9, "trusted", false, "cc")});
    QVERIFY(!settableAt(m, 0));
    QVERIFY(settableAt(m, 1));
    QVERIFY(!m.data(m.index(1), OmemoDevicesModel::ActiveRole).toBool());
}

// Every account's events reach every model, so the wrong account or the wrong
// subject must not repaint this one.
void TestOmemoDevices::eventsAreScopedToAccountAndJid() {
    OmemoDevicesModel m;
    own(m);
    const QVariantList rows{row(8, "trusted", true, "bb")};

    m.handleEvent("omemo", "TrustList",
                  QVariantMap{{"acc", "other@h"}, {"jid", "me@h"}, {"trustList", rows}});
    QCOMPARE(m.rowCount(), 0);
    m.handleEvent("omemo", "TrustList",
                  QVariantMap{{"acc", "me@h"}, {"jid", "friend@h"}, {"trustList", rows}});
    QCOMPARE(m.rowCount(), 0);
    m.handleEvent("omemo", "TrustList",
                  QVariantMap{{"acc", "me@h"}, {"jid", "me@h"}, {"trustList", rows}});
    QCOMPARE(m.rowCount(), 1);

    QSignalSpy blind(&m, &OmemoDevicesModel::blindTrustChanged);
    m.handleEvent("omemo", "BlindTrust", QVariantMap{{"acc", "other@h"}, {"value", true}});
    QVERIFY(!m.blindTrust());
    m.handleEvent("omemo", "BlindTrust", QVariantMap{{"acc", "me@h"}, {"value", true}});
    QVERIFY(m.blindTrust());
    QCOMPARE(blind.count(), 1);
}

// Same model against a contact: there is no own device to hide.
void TestOmemoDevices::peerListKeepsEveryDevice() {
    OmemoDevicesModel m;
    m.setAccount("me@h");
    m.setJid("friend@h");
    m.applyOwnDevice(7);
    m.applyTrustList({row(7, "trusted", true, "aa"), row(8, "undecided", true, "bb")});
    QCOMPARE(m.rowCount(), 2); // 7 is our device id, but this is their list
}

// Real backend: an account that has never connected has no keys on file, and
// asking for them is answered rather than refused.
void TestOmemoDevices::integrationEmptyListForFreshAccount() {
    TackyBackend backend;
    QVERIFY(backend.start());
    backend.notify("account", "add",
                   QVariantMap{{"acc", "me@example.com"},
                               {"password", "x"},
                               {"domain", "example.com"},
                               {"username", "me"}});

    OmemoDevicesModel m;
    m.setBackend(&backend);
    own(m, "me@example.com");

    QSignalSpy errors(&backend, &TackyBackend::error);
    QSignalSpy results(&backend, &TackyBackend::result);
    QTRY_VERIFY_WITH_TIMEOUT(results.count() >= 4, 5000); // trustList, blind, fp, device
    QCOMPARE(errors.count(), 0);
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.ownFingerprint(), QString()); // no store until the client is ready

    backend.stop();
}

QTEST_MAIN(TestOmemoDevices)
#include "tst_omemodevices.moc"
