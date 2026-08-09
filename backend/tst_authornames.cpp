#include <QtTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>

#include "AuthorNames.h"
#include "TackyBackend.h"

static void feedEvent(AuthorNames &a, const QByteArray &json) {
    const QJsonArray arr = QJsonDocument::fromJson(json).array();
    a.handleEvent(arr.at(1).toString(), arr.at(2).toString(), arr.at(3).toVariant());
}

static QVariantMap map(const QByteArray &json) {
    return QJsonDocument::fromJson(json).object().toVariantMap();
}

class TestAuthorNames : public QObject {
    Q_OBJECT
private slots:
    void fetchesOnChat();
    void changedUpdatesOneSender();
    void filtersOtherChats();
    void chatSwitchDropsTheOldMap();
};

void TestAuthorNames::fetchesOnChat() {
    TackyBackend backend;
    AuthorNames a;
    a.setBackend(&backend);
    a.setAccount("me@h");
    a.setChat("room@h"); // issues `author get` as token 1

    QSignalSpy changed(&a, &AuthorNames::namesChanged);
    a.handleResult(1, map(R"({"room@h/ann":"Ann","room@h/bo":"Bo"})"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(a.names().value("room@h/ann").toString(), QString("Ann"));

    // A reply from a request we no longer care about is not ours to apply.
    a.handleResult(7, map(R"({"room@h/ann":"Someone Else"})"));
    QCOMPARE(a.names().value("room@h/ann").toString(), QString("Ann"));
}

void TestAuthorNames::changedUpdatesOneSender() {
    TackyBackend backend;
    AuthorNames a;
    a.setBackend(&backend);
    a.setAccount("me@h");
    a.setChat("room@h");
    a.handleResult(1, map(R"({"room@h/ann":"Ann","room@h/bo":"Bo"})"));

    QSignalSpy changed(&a, &AuthorNames::namesChanged);
    feedEvent(a, R"(["event","author","Changed",
        {"acc":"me@h","chat":"room@h","from":"room@h/ann","name":"Annabel"}])");
    QCOMPARE(changed.count(), 1);
    QCOMPARE(a.names().value("room@h/ann").toString(), QString("Annabel"));
    QCOMPARE(a.names().value("room@h/bo").toString(), QString("Bo")); // untouched

    // A sender the map never had is an arrival, not a correction.
    feedEvent(a, R"(["event","author","Changed",
        {"acc":"me@h","chat":"room@h","from":"room@h/cy","name":"Cy"}])");
    QCOMPARE(a.names().value("room@h/cy").toString(), QString("Cy"));
}

void TestAuthorNames::filtersOtherChats() {
    TackyBackend backend;
    AuthorNames a;
    a.setBackend(&backend);
    a.setAccount("me@h");
    a.setChat("room@h");
    a.handleResult(1, map(R"({"room@h/ann":"Ann"})"));

    feedEvent(a, R"(["event","author","Changed",
        {"acc":"other@h","chat":"room@h","from":"room@h/ann","name":"Wrong"}])");
    feedEvent(a, R"(["event","author","Changed",
        {"acc":"me@h","chat":"elsewhere@h","from":"room@h/ann","name":"Wrong"}])");
    QCOMPARE(a.names().value("room@h/ann").toString(), QString("Ann"));
}

// Names are per chat, so carrying them over would label the new conversation
// with the previous one's senders until its own map arrived.
void TestAuthorNames::chatSwitchDropsTheOldMap() {
    TackyBackend backend;
    AuthorNames a;
    a.setBackend(&backend);
    a.setAccount("me@h");
    a.setChat("room@h");
    a.handleResult(1, map(R"({"room@h/ann":"Ann"})"));
    QVERIFY(!a.names().isEmpty());

    a.setChat("other@h");
    QVERIFY(a.names().isEmpty());
}

QTEST_MAIN(TestAuthorNames)
#include "tst_authornames.moc"
