// One in-band registration (XEP-0077): sign-up on a server the app has no
// account on yet. tacky runs it over a throwaway connection kept out of the
// account store, so nothing exists locally until the caller adds the account
// itself - see registeredJid(), which is the handoff.
//
// The rows are the form's fields, in server order, one per visible field:
// tacky decides what a server asks for and only says which type each answer
// is. Hidden fields are not rows - tacky keeps its own copy of the form and
// only takes the vars we name back, so FORM_TYPE and friends round-trip
// without passing through here. `var` is spelled `name`, because a QML
// delegate cannot declare a property called var.
//
// One session per instance, keyed by a token tacky maps to its own; a second
// window signing up somewhere else is a second instance. QtCore only.
#ifndef REGISTRATIONCONTROLLER_H
#define REGISTRATIONCONTROLLER_H

#include <QHash>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "MapListModel.h"

class TackyBackend;

class RegistrationController : public MapListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString host READ host NOTIFY hostChanged)
    // What went wrong, from tacky. Set with state Failed and cleared by the
    // next start()/retry(), so the two are read together.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString title READ title NOTIFY formChanged)
    // There are fields to answer, i.e. the sign-up is past naming a server.
    Q_PROPERTY(bool hasForm READ hasForm NOTIFY formChanged)
    Q_PROPERTY(QString instructions READ instructions NOTIFY instructionsChanged)
    // Every required field has an answer, i.e. there is a point in submitting.
    Q_PROPERTY(bool complete READ isComplete NOTIFY completeChanged)
    // The account that now exists on the server, "" until it does - and also
    // when the server asked under names we cannot read a JID out of.
    Q_PROPERTY(QString registeredJid READ registeredJid NOTIFY stateChanged)

public:
    enum State {
        Idle,       // nothing started, or cancelled
        Connecting, // asked, no form yet
        Ready,      // form in hand, being filled in
        Submitting, // answers sent, no verdict yet
        Registered, // the account exists on the server
        Failed,     // see error(); rows survive so the answers do
    };
    Q_ENUM(State)

    enum Role {
        NameRole = Qt::UserRole + 1, // the field's var
        TypeRole,                    // XEP-0004 field type, e.g. text-private
        LabelRole,
        RequiredRole,
        ValueRole,       // the answer, for the single-value types
        ValuesRole,      // the answer, for the -multi types
        OptionsRole,     // [{label,value}] for the list types, else empty
        MediaSourceRole, // a data: URL once the CAPTCHA bytes land, else ""
        HasMediaRole,    // the field carries a picture, landed or not
    };
    Q_ENUM(Role)

    explicit RegistrationController(QObject *parent = nullptr);
    ~RegistrationController() override;

    TackyBackend *backend() const { return m_backend; }
    void setBackend(TackyBackend *backend);

    State state() const { return m_state; }
    QString host() const { return m_host; }
    QString error() const { return m_error; }
    QString title() const { return m_title; }
    bool hasForm() const { return rowCount() > 0; }
    QString instructions() const { return m_instructions; }
    bool isComplete() const;
    QString registeredJid() const;
    // The session tag tacky keys its side by; unique per instance.
    QString token() const { return m_token; }

    // Ask `host` for its registration form. Port 0 leaves tacky its default.
    // Replaces whatever this instance was doing, answers included.
    Q_INVOKABLE void start(const QString &host, int port = 0);

    // Ask the same host again, keeping the answers typed so far. What a failed
    // submit needs: the session it failed in is spent, and a CAPTCHA in the
    // form it was holding has expired with it.
    Q_INVOKABLE void retry();

    // Answer one field. A string for the single-value types, a list for the
    // -multi ones; either is accepted for either, and stored as both.
    Q_INVOKABLE void setValue(int row, const QVariant &value);

    // Send the answers. The verdict arrives as an event, not a reply.
    Q_INVOKABLE void submitForm();

    // Drop the session, on this side and tacky's. Safe at any point.
    Q_INVOKABLE void cancel();

    Q_INVOKABLE QString valueFor(const QString &var) const;

    // Routing and the form transform are public so tests can drive them with
    // canned replies, as the other backend models are tested.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);
    void applyForm(const QVariantMap &form);

signals:
    void backendChanged();
    void stateChanged();
    void hostChanged();
    void errorChanged();
    void formChanged();
    void instructionsChanged();
    void completeChanged();

private:
    void onRunningChanged();
    void setState(State state);
    void setError(const QString &message);
    void connectSession();
    void requestForm();
    void requestMedia(const QString &var);
    void applyMedia(const QString &var, const QString &base64);
    // The answers, keyed by var, as sent to tacky. Also what survives a retry.
    QVariantMap answers() const;
    int rowOf(const QString &var) const;
    static bool isMulti(const QString &type);

    TackyBackend *m_backend = nullptr;
    QString m_token;
    QString m_host;
    QString m_error;
    QString m_title;
    QString m_instructions;
    State m_state = Idle;
    int m_port = 0;
    int m_formToken = 0;                // in-flight `register form`, 0 for none
    QHash<int, QString> m_mediaPending; // request token -> field var
    // Everything typed here, kept apart from the rows so a re-fetched form can
    // be filled back in. tacky's own restore only covers what the server sent.
    QVariantMap m_entered;
};

#endif // REGISTRATIONCONTROLLER_H
