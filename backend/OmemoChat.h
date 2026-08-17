// The OMEMO switch for one chat. Encryption is per conversation and on by
// default; tacky stores the choice and stamps every send with it, so all this
// holds is the current answer for the open chat.
//
// Read with `omemo isEnabled`, then followed with `omemo <Enabled>`. The read
// carries a token so a dead link answers with an error rather than with
// nothing. Until it answers `known` is false and there is no padlock to draw:
// the default is on, which over a chat that is really off would claim an
// encryption that is not happening.
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
    // Whether `enabled` is an answer yet, rather than the default standing in
    // for one.
    Q_PROPERTY(bool known READ known NOTIFY knownChanged)

public:
    explicit OmemoChat(QObject *parent = nullptr);

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString jid() const { return m_jid; }
    bool groupchat() const { return m_groupchat; }
    bool enabled() const { return m_enabled; }
    bool known() const { return m_known; }
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
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);
    void applyEnabled(bool on);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void groupchatChanged();
    void availableChanged();
    void enabledChanged();
    void knownChanged();

private:
    // Fetch the peer's device list and bundles, once per chat, so the first
    // message does not have to wait for them.
    void prepare();
    void setKnown(bool v);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;
    bool m_groupchat = false;
    // tacky's own default, and a placeholder until the read answers: nothing
    // should draw it while `known` is false.
    bool m_enabled = true;
    bool m_known = false;
    int m_readToken = -1;
    bool m_prepared = false; // this chat's keys have been asked for already
};

#endif // OMEMOCHAT_H
