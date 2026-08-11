// Driven with canned tacky replies, decoded exactly as TackyBackend decodes
// them. The join dialog reads this model's loading/loaded/error trio to decide
// what to say in place of an empty list, so each has to mean one thing.
#include <QtTest>
#include <QJsonDocument>
#include <QSignalSpy>

#include "MucRoomsModel.h"
#include "TackyBackend.h"

static QVariantList roomsFrom(const QByteArray &json) {
    return QJsonDocument::fromJson(json).array().toVariantList();
}

class TestMucRooms : public QObject {
    Q_OBJECT
private slots:
    void holdsRoomsInServerOrder();
    void discoverTracksLoadingAndLoaded();
    void discoverReportsAnError();
    void aSecondDiscoverAbandonsTheFirst();
    void defaultNickIsSeparateFromDiscovery();
    void switchingAccountForgetsEverything();
};

// The order a service lists its rooms in is its answer; sorting them here would
// be inventing one.
void TestMucRooms::holdsRoomsInServerOrder() {
    MucRoomsModel m;
    m.applyRooms(roomsFrom(R"([
        {"jid":"zeta@muc.h","name":"Zeta","occupants":3},
        {"jid":"alpha@muc.h","name":"","occupants":41}
    ])"));
    QCOMPARE(m.rowCount(), 2);
    QCOMPARE(m.data(m.index(0), MucRoomsModel::JidRole).toString(),
             QString("zeta@muc.h"));
    QCOMPARE(m.data(m.index(1), MucRoomsModel::OccupantsRole).toInt(), 41);
    QCOMPARE(m.data(m.index(1), MucRoomsModel::NameRole).toString(), QString());
    QCOMPARE(m.roleNames().value(MucRoomsModel::OccupantsRole),
             QByteArray("occupants"));
}

// An empty list means "this service hosts nothing" only once a discovery has
// come back; before that it means nobody has asked.
void TestMucRooms::discoverTracksLoadingAndLoaded() {
    TackyBackend backend;
    MucRoomsModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    QVERIFY(!m.loading());
    QVERIFY(!m.loaded());

    QSignalSpy sent(&backend, &TackyBackend::sent);
    QSignalSpy loading(&m, &MucRoomsModel::loadingChanged);
    m.discover("conference.h");
    QVERIFY(m.loading());
    QCOMPARE(loading.count(), 1);
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.at(0).at(1).toString(), QString("discoverRooms"));
    QCOMPARE(sent.at(0).at(2).toMap().value("jid").toString(),
             QString("conference.h"));

    m.handleResult(1, roomsFrom(R"([{"jid":"a@muc.h","name":"A","occupants":1}])"));
    QVERIFY(!m.loading());
    QVERIFY(m.loaded());
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.error(), QString());
}

void TestMucRooms::discoverReportsAnError() {
    TackyBackend backend;
    MucRoomsModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.discover("conference.h"); // token 1

    // Through the signal, so the connection is under test too.
    emit backend.error(1, "service-unavailable");
    QCOMPARE(m.error(), QString("service-unavailable"));
    QVERIFY(!m.loading());
    // A failure is not an answer: the hint has to say why, not "no rooms".
    QVERIFY(!m.loaded());

    // Asking again clears the last complaint before it makes a new one.
    m.discover("other.h");
    QCOMPARE(m.error(), QString());
}

// Discover replaces the list. A slow first answer landing after a second went
// out would otherwise show one service's rooms under another's name.
void TestMucRooms::aSecondDiscoverAbandonsTheFirst() {
    TackyBackend backend;
    MucRoomsModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    m.discover("slow.h");  // token 1
    m.discover("quick.h"); // token 2
    m.handleResult(2, roomsFrom(R"([{"jid":"q@quick.h","name":"Q","occupants":2}])"));
    QCOMPARE(m.rowCount(), 1);

    m.handleResult(1, roomsFrom(R"([{"jid":"s@slow.h","name":"S","occupants":9}])"));
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.data(m.index(0), MucRoomsModel::JidRole).toString(),
             QString("q@quick.h"));
}

// The nick reply is a bare string on the same result channel as the room list;
// telling them apart is the token's job, not the shape's.
void TestMucRooms::defaultNickIsSeparateFromDiscovery() {
    TackyBackend backend;
    MucRoomsModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");

    QSignalSpy nick(&m, &MucRoomsModel::defaultNickChanged);
    m.requestDefaultNick(); // token 1
    m.discover("conference.h"); // token 2
    m.handleResult(1, QVariant(QStringLiteral("romeo")));
    QCOMPARE(m.defaultNick(), QString("romeo"));
    QCOMPARE(nick.count(), 1);
    // Not mistaken for a (very short) room list.
    QCOMPARE(m.rowCount(), 0);
    QVERIFY(m.loading());

    // No stored nick is a gap the dialog fills, not an error to show.
    MucRoomsModel m2;
    m2.setBackend(&backend);
    m2.setAccount("me@h");
    m2.requestDefaultNick();
    emit backend.error(3, "nope");
    QCOMPARE(m2.error(), QString());
}

void TestMucRooms::switchingAccountForgetsEverything() {
    TackyBackend backend;
    MucRoomsModel m;
    m.setBackend(&backend);
    m.setAccount("me@h");
    m.requestDefaultNick(); // token 1
    m.discover("conference.h"); // token 2
    m.handleResult(1, QVariant(QStringLiteral("romeo")));
    m.handleResult(2, roomsFrom(R"([{"jid":"a@muc.h","name":"A","occupants":1}])"));
    QCOMPARE(m.rowCount(), 1);

    // Another account's server hosts other rooms, and its nick is its own.
    m.setAccount("other@h");
    QCOMPARE(m.rowCount(), 0);
    QVERIFY(!m.loaded());
    QCOMPARE(m.defaultNick(), QString());
}

QTEST_MAIN(TestMucRooms)
#include "tst_mucrooms.moc"
