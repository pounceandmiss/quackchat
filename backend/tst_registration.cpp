// The sign-up, driven over a stand-in transport with canned tacky frames: the
// real one would need a server that hands out accounts. What is pinned here is
// the order of the handshake (connect, then a <Form> event, then the read that
// actually carries the fields), that answers survive a re-fetched form, and
// that an abandoned session is always let go of - tacky holds a connection
// open per session, and only a cancel closes it.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "RegistrationController.h"
#include "TackyBackend.h"
#include "TackyTransport.h"

// Records what went out and plays back what tacky would have said.
class FakeTransport : public TackyTransport {
    Q_OBJECT
public:
    bool start(const QStringList &) override {
        m_connected = true;
        emit connectedChanged();
        return true;
    }
    void stop() override {
        if (!m_connected)
            return;
        m_connected = false;
        emit connectedChanged();
    }
    bool isConnected() const override { return m_connected; }
    void send(const QByteArray &json) override { sent.append(json); }

    void deliver(const QByteArray &json) { emit received(QString::fromUtf8(json)); }

    // The frames named module/method, oldest first.
    QList<QJsonArray> callsTo(const QString &method) const {
        QList<QJsonArray> out;
        for (const QByteArray &f : sent) {
            const QJsonArray a = QJsonDocument::fromJson(f).array();
            if (a.at(1).toString() == method)
                out.append(a);
        }
        return out;
    }
    QJsonObject argsOfLast(const QString &method) const {
        const QList<QJsonArray> calls = callsTo(method);
        return calls.isEmpty() ? QJsonObject() : calls.last().at(2).toObject();
    }
    int tokenOfLast(const QString &method) const {
        const QList<QJsonArray> calls = callsTo(method);
        return calls.isEmpty() ? -1 : calls.last().at(3).toInt();
    }

    QList<QByteArray> sent;

private:
    bool m_connected = false;
};

// A form with one of everything that changes how a field is handled: a hidden
// field nobody answers, a fixed line, a required box, a private one, a list,
// and a question with a picture.
static const char kForm[] = R"({
    "instructions": "Choose a name",
    "fields": [
        {"var":"FORM_TYPE","type":"hidden","label":"","required":false,
         "value":["jabber:iq:register"]},
        {"var":"intro","type":"fixed","label":"intro","required":false,
         "value":["Accounts are free"]},
        {"var":"username","type":"text-single","label":"Username",
         "required":true,"value":[]},
        {"var":"password","type":"text-private","label":"Password",
         "required":true,"value":[]},
        {"var":"tier","type":"list-single","label":"Tier","required":false,
         "value":["free"],
         "options":[{"label":"Free","value":"free"},
                    {"label":"Paid","value":"paid"}]},
        {"var":"ocr","type":"text-single","label":"Type the word",
         "required":true,"value":[],"media":{"cid":"c1","type":"image/jpeg"}}
    ]
})";

class TestRegistration : public QObject {
    Q_OBJECT
private slots:
    void rowsAreTheVisibleFieldsInServerOrder();
    void theFormIsARoundTripBehindTheEvent();
    void anotherSessionsEventsAreNotOurs();
    void mediaArrivesAsADataUrl();
    void completeFollowsTheRequiredFields();
    void answersCarryEveryFieldTheUserCouldAnswer();
    void aMultiFieldGoesOverAsAList();
    void answersSurviveARefetchedForm();
    void aFailedSubmitKeepsTheFormAndSaysWhy();
    void registeredJidPairsTheUsernameWithTheHost();
    void anAbandonedSignUpIsCancelled();
    void aStoppedBackendEndsTheWait();
};

// Builds a controller on a live fake transport, with the form already in hand.
struct Fixture {
    TackyBackend backend;
    FakeTransport *wire = new FakeTransport;
    RegistrationController reg;

    Fixture() {
        backend.setTransport(wire);
        backend.start();
        reg.setBackend(&backend);
    }

    // connect -> <Form> -> the form read and its reply.
    void toForm(const char *form = kForm) {
        reg.start(QStringLiteral("example.com"));
        deliverEvent("Form");
        answerForm(form);
    }
    void deliverEvent(const char *name, const QJsonObject &extra = {}) {
        QJsonObject args = extra;
        args.insert("token", reg.token());
        wire->deliver(QJsonDocument(QJsonArray{"event", "register", name, args})
                          .toJson(QJsonDocument::Compact));
    }
    void answerForm(const char *form) {
        const int tok = wire->tokenOfLast("form");
        wire->deliver(
            QJsonDocument(QJsonArray{"result", tok,
                                     QJsonDocument::fromJson(form).object()})
                .toJson(QJsonDocument::Compact));
    }
};

// Hidden fields are tacky's business, not the user's: it submits from its own
// copy of the form, so FORM_TYPE round-trips without being shown or answered.
void TestRegistration::rowsAreTheVisibleFieldsInServerOrder() {
    RegistrationController reg;
    reg.applyForm(QJsonDocument::fromJson(kForm).object().toVariantMap());

    QCOMPARE(reg.rowCount(), 5);
    QCOMPARE(reg.data(reg.index(0), RegistrationController::NameRole).toString(),
             QString("intro"));
    QCOMPARE(reg.data(reg.index(0), RegistrationController::TypeRole).toString(),
             QString("fixed"));
    QCOMPARE(reg.data(reg.index(0), RegistrationController::ValueRole).toString(),
             QString("Accounts are free"));
    QCOMPARE(reg.data(reg.index(1), RegistrationController::NameRole).toString(),
             QString("username"));
    QVERIFY(reg.data(reg.index(1), RegistrationController::RequiredRole).toBool());
    QVERIFY(!reg.data(reg.index(3), RegistrationController::RequiredRole).toBool());
    QCOMPARE(reg.instructions(), QString("Choose a name"));

    // A list field keeps its options and the value the server preselected.
    const QVariantList options =
        reg.data(reg.index(3), RegistrationController::OptionsRole).toList();
    QCOMPARE(options.size(), 2);
    QCOMPARE(options.at(1).toMap().value("label").toString(), QString("Paid"));
    QCOMPARE(reg.data(reg.index(3), RegistrationController::ValueRole).toString(),
             QString("free"));

    // Only the CAPTCHA says it has a picture, and it has no bytes yet.
    QVERIFY(!reg.data(reg.index(1), RegistrationController::HasMediaRole).toBool());
    QVERIFY(reg.data(reg.index(4), RegistrationController::HasMediaRole).toBool());
    QCOMPARE(reg.data(reg.index(4), RegistrationController::MediaSourceRole).toString(),
             QString());

    // The role names are what a QML delegate declares, `var` spelled `name`.
    QCOMPARE(reg.roleNames().value(RegistrationController::NameRole),
             QByteArray("name"));
    QCOMPARE(reg.roleNames().value(RegistrationController::MediaSourceRole),
             QByteArray("mediaSource"));
}

// The <Form> event says a form is ready, not what is in it: the fields are a
// separate read, and until it answers there is nothing to draw.
void TestRegistration::theFormIsARoundTripBehindTheEvent() {
    Fixture f;
    QSignalSpy states(&f.reg, &RegistrationController::stateChanged);

    f.reg.start(QStringLiteral("example.com"));
    QCOMPARE(f.reg.state(), RegistrationController::Connecting);
    QCOMPARE(f.wire->argsOfLast("connect").value("host").toString(),
             QString("example.com"));
    QCOMPARE(f.wire->argsOfLast("connect").value("token").toString(), f.reg.token());
    QVERIFY(f.wire->callsTo("form").isEmpty());

    f.deliverEvent("Form");
    QCOMPARE(f.wire->callsTo("form").size(), 1);
    QCOMPARE(f.reg.state(), RegistrationController::Connecting);
    QVERIFY(!f.reg.hasForm());

    f.answerForm(kForm);
    QCOMPARE(f.reg.state(), RegistrationController::Ready);
    QVERIFY(f.reg.hasForm());
    QCOMPARE(f.reg.rowCount(), 5);
    QVERIFY(states.count() >= 2);
}

// Two sign-ups can be open at once, so every event names the session it is
// about and the ones that are not ours are somebody else's form.
void TestRegistration::anotherSessionsEventsAreNotOurs() {
    Fixture f;
    f.reg.start(QStringLiteral("example.com"));

    f.wire->deliver(QJsonDocument(QJsonArray{
                                      "event", "register", "Form",
                                      QJsonObject{{"token", "someone-else"}}})
                        .toJson(QJsonDocument::Compact));
    QVERIFY(f.wire->callsTo("form").isEmpty());

    f.wire->deliver(QJsonDocument(QJsonArray{
                                      "event", "register", "Error",
                                      QJsonObject{{"token", "someone-else"},
                                                  {"message", "not yours"}}})
                        .toJson(QJsonDocument::Compact));
    QCOMPARE(f.reg.error(), QString());
    QCOMPARE(f.reg.state(), RegistrationController::Connecting);
}

// The picture is a third round trip. It lands as base64 and goes into the row
// as a data: URL, which is what an Image can be pointed at directly.
void TestRegistration::mediaArrivesAsADataUrl() {
    Fixture f;
    f.toForm();

    f.deliverEvent("MediaReady", QJsonObject{{"var", "ocr"}});
    QCOMPARE(f.wire->argsOfLast("media").value("var").toString(), QString("ocr"));

    QSignalSpy changed(&f.reg, &RegistrationController::dataChanged);
    f.wire->deliver(QJsonDocument(QJsonArray{"result", f.wire->tokenOfLast("media"),
                                             "AAAB"})
                        .toJson(QJsonDocument::Compact));
    // The mime type is the one the field named, not a guess.
    QCOMPARE(f.reg.data(f.reg.index(4), RegistrationController::MediaSourceRole)
                 .toString(),
             QString("data:image/jpeg;base64,AAAB"));
    QCOMPARE(changed.count(), 1);
}

// The submit button reads this, so an unanswered required field has to be the
// only thing that holds it back - an optional one never does.
void TestRegistration::completeFollowsTheRequiredFields() {
    Fixture f;
    f.toForm();
    QVERIFY(!f.reg.isComplete());

    QSignalSpy complete(&f.reg, &RegistrationController::completeChanged);
    f.reg.setValue(1, QStringLiteral("alice"));
    f.reg.setValue(2, QStringLiteral("hunter2"));
    QVERIFY(!f.reg.isComplete()); // the CAPTCHA is required too
    f.reg.setValue(4, QStringLiteral("swordfish"));
    QVERIFY(f.reg.isComplete());
    QCOMPARE(complete.count(), 3);
}

// Every field the user could have answered goes back, emptied ones included:
// leaving one out would keep whatever the server had put there.
void TestRegistration::answersCarryEveryFieldTheUserCouldAnswer() {
    Fixture f;
    f.toForm();
    f.reg.setValue(1, QStringLiteral("alice"));
    f.reg.setValue(2, QStringLiteral("hunter2"));

    f.reg.submitForm();
    QCOMPARE(f.reg.state(), RegistrationController::Submitting);
    const QJsonObject values = f.wire->argsOfLast("submit").value("values").toObject();
    QCOMPARE(values.value("username").toString(), QString("alice"));
    QCOMPARE(values.value("password").toString(), QString("hunter2"));
    QCOMPARE(values.value("tier").toString(), QString("free"));
    QVERIFY(values.contains("ocr")); // never typed, still answered
    QVERIFY(!values.contains("intro"));    // a line of prose is not an answer
    QVERIFY(!values.contains("FORM_TYPE")); // tacky's own to round-trip
}

// tacky's form code wants a Tcl list for the -multi types, which is what a
// JSON array decodes to; the single-value types take one string.
void TestRegistration::aMultiFieldGoesOverAsAList() {
    Fixture f;
    f.reg.start(QStringLiteral("example.com"));
    f.deliverEvent("Form");
    f.answerForm(R"({"fields":[
        {"var":"rooms","type":"list-multi","label":"Rooms","required":false,
         "value":[],"options":[{"label":"A","value":"a"},
                               {"label":"B","value":"b"}]}
    ]})");

    f.reg.setValue(0, QVariant(QStringList{"a", "b"}));
    QCOMPARE(f.reg.data(f.reg.index(0), RegistrationController::ValuesRole)
                 .toStringList(),
             QStringList({"a", "b"}));
    f.reg.submitForm();
    const QJsonArray rooms = f.wire->argsOfLast("submit")
                                 .value("values")
                                 .toObject()
                                 .value("rooms")
                                 .toArray();
    QCOMPARE(rooms.size(), 2);
    QCOMPARE(rooms.at(1).toString(), QString("b"));
}

// A CAPTCHA expires with the session it came in, so a failed submit is retried
// against a form fetched afresh - with what was typed put back into it, which
// tacky cannot do for us: its own restore only carries what the server sent.
void TestRegistration::answersSurviveARefetchedForm() {
    Fixture f;
    f.toForm();
    f.reg.setValue(1, QStringLiteral("alice"));
    f.reg.setValue(2, QStringLiteral("hunter2"));
    f.reg.setValue(4, QStringLiteral("wrong"));
    f.reg.submitForm();
    f.deliverEvent("Error", QJsonObject{{"message", "captcha failed"}});

    f.reg.retry();
    QCOMPARE(f.reg.state(), RegistrationController::Connecting);
    QCOMPARE(f.wire->callsTo("connect").size(), 2);
    QCOMPARE(f.wire->argsOfLast("connect").value("host").toString(),
             QString("example.com"));
    f.deliverEvent("Form");
    f.answerForm(kForm);

    QCOMPARE(f.reg.valueFor(QStringLiteral("username")), QString("alice"));
    QCOMPARE(f.reg.valueFor(QStringLiteral("password")), QString("hunter2"));
    // The one the server rejected comes back too, to be corrected rather than
    // retyped - and the picture that went with it is gone.
    QCOMPARE(f.reg.valueFor(QStringLiteral("ocr")), QString("wrong"));
    QCOMPARE(f.reg.data(f.reg.index(4), RegistrationController::MediaSourceRole)
                 .toString(),
             QString());
}

// The form has to survive a rejection: it is holding the answers, and the user
// has one thing to fix, not a page to fill in again.
void TestRegistration::aFailedSubmitKeepsTheFormAndSaysWhy() {
    Fixture f;
    f.toForm();
    f.reg.setValue(1, QStringLiteral("alice"));
    f.reg.submitForm();

    f.deliverEvent("Error", QJsonObject{{"message", "username taken"}});
    QCOMPARE(f.reg.state(), RegistrationController::Failed);
    QCOMPARE(f.reg.error(), QString("username taken"));
    QCOMPARE(f.reg.rowCount(), 5);
    QCOMPARE(f.reg.valueFor(QStringLiteral("username")), QString("alice"));

    // And submitting again is allowed: the session is still tacky's to answer.
    f.reg.setValue(1, QStringLiteral("alice2"));
    f.reg.submitForm();
    QCOMPARE(f.reg.state(), RegistrationController::Submitting);
    QCOMPARE(f.reg.error(), QString());
    QCOMPARE(f.wire->callsTo("submit").size(), 2);
}

// tacky says only that the account exists; which account it is has to be read
// out of the answers, and a server that never asked for a username has not
// said. Callers add the account, so an empty answer must be an empty JID
// rather than a guess.
void TestRegistration::registeredJidPairsTheUsernameWithTheHost() {
    Fixture f;
    f.toForm();
    f.reg.setValue(1, QStringLiteral("alice"));
    f.reg.submitForm();
    QCOMPARE(f.reg.registeredJid(), QString()); // not yet, and not from Ready
    f.deliverEvent("Success");
    QCOMPARE(f.reg.state(), RegistrationController::Registered);
    QCOMPARE(f.reg.registeredJid(), QString("alice@example.com"));

    Fixture g;
    g.reg.start(QStringLiteral("example.com"));
    g.deliverEvent("Form");
    g.answerForm(R"({"fields":[
        {"var":"email","type":"text-single","label":"Email","required":true,
         "value":[]}]})");
    g.reg.setValue(0, QStringLiteral("alice@elsewhere.example"));
    g.reg.submitForm();
    g.deliverEvent("Success");
    QCOMPARE(g.reg.registeredJid(), QString());
}

// tacky keeps a connection open per session and closes it when told to. Both
// ways out of the sheet have to tell it: the one the user takes, and the one
// where the sheet is destroyed under them.
void TestRegistration::anAbandonedSignUpIsCancelled() {
    Fixture f;
    f.toForm();
    f.reg.cancel();
    QCOMPARE(f.wire->callsTo("cancel").size(), 1);
    QCOMPARE(f.wire->argsOfLast("cancel").value("token").toString(), f.reg.token());
    QCOMPARE(f.reg.state(), RegistrationController::Idle);
    QCOMPARE(f.reg.rowCount(), 0);
    QVERIFY(!f.reg.hasForm());

    // Cancelled twice is not cancelled again: there is no session left to end.
    f.reg.cancel();
    QCOMPARE(f.wire->callsTo("cancel").size(), 1);

    TackyBackend backend;
    FakeTransport *wire = new FakeTransport;
    backend.setTransport(wire);
    backend.start();
    {
        RegistrationController reg;
        reg.setBackend(&backend);
        reg.start(QStringLiteral("example.com"));
    }
    QCOMPARE(wire->callsTo("cancel").size(), 1);
}

// The session lived in the backend, so when that goes the sign-up is over -
// and saying so is the only way the sheet stops waiting on a form.
void TestRegistration::aStoppedBackendEndsTheWait() {
    Fixture f;
    f.toForm();
    f.backend.stop();
    QCOMPARE(f.reg.state(), RegistrationController::Failed);
    QVERIFY(!f.reg.error().isEmpty());
}

QTEST_MAIN(TestRegistration)
#include "tst_registration.moc"
