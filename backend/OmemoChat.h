// The OMEMO switch for one chat. Encryption is per conversation, on by default
// for a 1:1 and off for a room; tacky stores the choice and stamps every send
// with it, so all this holds is the current answer for the open chat.
//
// A 1:1 is read with `omemo isEnabled`, a room with `omemo roomStatus`, which
// also says whether the room can be encrypted at all and who stopped the last
// send. Both are followed with `omemo <Enabled>`, a room with <RoomStatus> and
// <MembersUnreachable> too. The read carries a token so a dead link answers
// with an error rather than with nothing. Until it answers `known` is false and
// there is no padlock to draw: the default is a guess, which over a chat set
// the other way would misreport how the next message goes out.
#ifndef OMEMOCHAT_H
#define OMEMOCHAT_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class OmemoChat : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString jid READ jid WRITE setJid NOTIFY jidChanged)
    Q_PROPERTY(bool groupchat READ groupchat WRITE setGroupchat NOTIFY groupchatChanged)
    // Whether this chat can be encrypted at all, which is what decides if the
    // control is drawn. For a room that is tacky's `offered`: it qualifies, or
    // it is on, and one that stopped qualifying keeps its switch so it can be
    // turned off.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // Whether `enabled` is an answer yet, rather than the default standing in
    // for one.
    Q_PROPERTY(bool known READ known NOTIFY knownChanged)

    // A room's side of it, from `omemo roomStatus`; empty for a 1:1.
    // `eligible` is whether the room qualifies, and `reasons` why not:
    // unknown, not_members_only, anonymous.
    Q_PROPERTY(bool eligible READ eligible NOTIFY roomStatusChanged)
    Q_PROPERTY(QStringList reasons READ reasons NOTIFY roomStatusChanged)
    // How much of the member list the room gave out: pending, complete,
    // partial, presence or none.
    Q_PROPERTY(QString memberList READ memberList NOTIFY roomStatusChanged)
    // How many members need attention: they stop or would hold up a send,
    // or a key of theirs changed. Who they are is on the room's people.
    Q_PROPERTY(int attention READ attention NOTIFY roomStatusChanged)
    // Who stopped the last send: [{jid, reason}], reason being no_devices or
    // no_usable_device. Kept by tacky until a send goes through.
    Q_PROPERTY(QVariantList unreachable READ unreachable NOTIFY unreachableChanged)

public:
    explicit OmemoChat(QObject *parent = nullptr);

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString jid() const { return m_jid; }
    bool groupchat() const { return m_groupchat; }
    bool enabled() const { return m_enabled; }
    bool known() const { return m_known; }
    bool available() const {
        if (m_account.isEmpty() || m_jid.isEmpty())
            return false;
        return !m_groupchat || (m_known && m_offered);
    }
    bool eligible() const { return m_eligible; }
    QStringList reasons() const { return m_reasons; }
    QString memberList() const { return m_memberList; }
    int attention() const { return m_attention; }
    QVariantList unreachable() const { return m_unreachable; }

    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setJid(const QString &jid);
    void setGroupchat(bool v);
    void setEnabled(bool on);

    // Re-read the toggle for the current chat.
    Q_INVOKABLE void refresh();
    // Put away the list of who stopped the last send. tacky keeps it until a
    // send goes through; this only stops showing it.
    Q_INVOKABLE void dismissUnreachable();

    // Routing and transforms are public so tests can drive them directly.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);
    void applyEnabled(bool on);
    void applyRoomStatus(const QVariantMap &status);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void groupchatChanged();
    void availableChanged();
    void enabledChanged();
    void knownChanged();
    void roomStatusChanged();
    void unreachableChanged();

private:
    // Fetch the peer's device list and bundles, once per chat, so the first
    // message does not have to wait for them.
    void prepare();
    void setKnown(bool v);
    void setUnreachable(const QVariantList &members);
    // Emits availableChanged if `was` no longer holds.
    void availableFrom(bool was);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;
    bool m_groupchat = false;
    // tacky's own default, and a placeholder until the read answers: nothing
    // should draw it while `known` is false.
    bool m_enabled = true;
    bool m_known = false;
    int m_readToken = -1;
    // A room's switch goes on through a request, since tacky refuses it for a
    // room that has stopped qualifying, and the refusal has to put it back.
    int m_setToken = -1;
    bool m_eligible = false;
    QStringList m_reasons;
    QString m_memberList;
    int m_attention = 0;
    bool m_offered = false;
    QVariantList m_unreachable;
    bool m_prepared = false; // this chat's keys have been asked for already
};

#endif // OMEMOCHAT_H
