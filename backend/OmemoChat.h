// The OMEMO switch for one chat. Encryption is per conversation and on by
// default; tacky stores the choice and stamps every send with it, so all this
// holds is the current answer for the open chat.
//
// There is no getter for it - tacky keeps the setting behind `omemo <Enabled>`
// deliberately - so the state is read by pulling that event and waiting for it
// to come back. Nothing here uses a token: every call is a notify, every answer
// an event.
#ifndef OMEMOCHAT_H
#define OMEMOCHAT_H

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class OmemoChat : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString jid READ jid WRITE setJid NOTIFY jidChanged)
    // OMEMO is for 1:1 only; a room always goes out in the clear.
    Q_PROPERTY(bool groupchat READ groupchat WRITE setGroupchat NOTIFY groupchatChanged)
    // Whether this chat can be encrypted at all, which is what decides if the
    // control is drawn.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit OmemoChat(QObject *parent = nullptr);

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString jid() const { return m_jid; }
    bool groupchat() const { return m_groupchat; }
    bool enabled() const { return m_enabled; }
    bool available() const {
        return !m_groupchat && !m_account.isEmpty() && !m_jid.isEmpty();
    }

    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setJid(const QString &jid);
    void setGroupchat(bool v);
    void setEnabled(bool on);

    // Re-read the toggle for the current chat.
    Q_INVOKABLE void refresh();

    // Routing and transforms are public so tests can drive them directly.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void applyEnabled(bool on);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void groupchatChanged();
    void availableChanged();
    void enabledChanged();

private:
    // Fetch the peer's device list and bundles, once per chat, so the first
    // message does not have to wait for them.
    void prepare();

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;
    bool m_groupchat = false;
    // Chats are encrypted unless the user says otherwise, and tacky answers the
    // same way for a chat it has never been asked about. Being wrong for the
    // one hop before the pull answers is only safe in this direction.
    bool m_enabled = true;
    bool m_prepared = false; // this chat's keys have been asked for already
};

#endif // OMEMOCHAT_H
