// One room's occupants, and the room-level state a details screen draws around
// them: the subject, the nick we are in there under, and whether we are in
// there at all. Seeded from `muc occupants` and kept live by the muc events.
//
// Every occupant arrives stamped with `caps` - what this account may do to that
// one - which tacky works out from the XEP-0045 role/affiliation rules and
// re-emits for the whole room whenever our own role moves. So the moderation
// buttons follow a promotion without anyone asking again, and the authorization
// policy stays in one place, which is not this one.
//
// Rows are grouped by role and sorted by nick within the group, which is what
// the Tk participant list draws; `filter` narrows what a crowded room shows
// without changing what it holds.
#ifndef MUCROOMMODEL_H
#define MUCROOMMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class MucRoomModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    // The chat JID as the chat list holds it, ?join suffix and all; what goes to
    // the muc module is the room JID under it.
    Q_PROPERTY(QString jid READ jid WRITE setJid NOTIFY jidChanged)
    Q_PROPERTY(QString roomJid READ roomJid NOTIFY jidChanged)
    // Substring of a nick or a real JID. View-side only: it never unsubscribes
    // anyone, so clearing it brings the whole room straight back.
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)

    Q_PROPERTY(QString subject READ subject NOTIFY subjectChanged)
    Q_PROPERTY(QString myNick READ myNick NOTIFY meChanged)
    // Our own row's OccupantJidRole, for the card that draws us outside the
    // list. Empty until there is a whole JID to give, since half of one is not
    // a JID at all and tacky rejects it.
    Q_PROPERTY(QString myOccupantJid READ myOccupantJid NOTIFY meChanged)
    // Read off our own row rather than asked for separately, so they cannot
    // disagree with the occupant the list is drawing for us.
    Q_PROPERTY(QString myRole READ myRole NOTIFY meChanged)
    Q_PROPERTY(QString myAffiliation READ myAffiliation NOTIFY meChanged)
    Q_PROPERTY(bool joined READ joined NOTIFY joinedChanged)

    // Rows after the filter, and occupants before it - "3 of 48" needs both.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    // group -> how many occupants are in it, before the filter. A section header
    // cannot count its own rows, and the count is about the room anyway.
    Q_PROPERTY(QVariantMap groupCounts READ groupCounts NOTIFY countChanged)

public:
    enum Role {
        NickRole = Qt::UserRole + 1,
        // room@service/nick: the JID their avatar lives under, and the one a
        // private message would go to.
        OccupantJidRole,
        // Their own JID, which a semi-anonymous room does not disclose.
        RealJidRole,
        RoleRole,        // moderator, participant, visitor, none
        AffiliationRole, // owner, admin, member, none, outcast
        ShowRole,        // "" (available), chat, away, xa, dnd
        StatusRole,      // whatever they typed as their presence text
        CapsRole,        // tacky's moderation flags against this occupant
        SelfRole,
        // Normalised role for grouping: moderator, participant, visitor, other.
        // The wording of the heading is the page's business, not this model's.
        GroupRole,
    };
    Q_ENUM(Role)

    explicit MucRoomModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    void setBackend(TackyBackend *backend);

    QString account() const { return m_account; }
    void setAccount(const QString &acc);

    QString jid() const { return m_jid; }
    void setJid(const QString &jid);
    QString roomJid() const { return m_roomJid; }

    QString filter() const { return m_filter; }
    void setFilter(const QString &text);

    QString subject() const { return m_subject; }
    QString myNick() const { return m_myNick; }
    QString myOccupantJid() const { return occupantJid(m_myNick); }
    QString myRole() const;
    QString myAffiliation() const;
    bool joined() const { return m_joined; }
    int total() const { return m_occupants.size(); }
    QVariantMap groupCounts() const;

    // (Re)read the whole room. Called on every change of subject below and
    // whenever the backend comes back.
    Q_INVOKABLE void refresh();

    // Moderation, by nick for roles and by real JID for affiliations - the split
    // is XEP-0045's, and tacky's methods keep it. Each answers, so a refusal
    // ("not-allowed", "forbidden") comes back through actionFailed rather than
    // disappearing.
    Q_INVOKABLE void kick(const QString &nick, const QString &reason = {});
    Q_INVOKABLE void setRole(const QString &nick, const QString &role,
                             const QString &reason = {});
    Q_INVOKABLE void setAffiliation(const QString &targetJid,
                                    const QString &affiliation,
                                    const QString &reason = {});
    Q_INVOKABLE void destroyRoom(const QString &reason = {});

    // Sent rather than asked: these are a message or a presence going out, with
    // no reply to wait for. What they did shows up as an event.
    Q_INVOKABLE void invite(const QString &jid, const QString &reason = {});
    Q_INVOKABLE void setSubject(const QString &text);
    Q_INVOKABLE void requestVoice();
    // Through the bookmark, as the Tk GUI does it: a nick typed once should be
    // the nick the next join uses too.
    Q_INVOKABLE void changeNick(const QString &nick);

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

    void applyOccupants(const QVariantList &occupants);
    void applyOccupant(const QVariantMap &occupant);
    void removeOccupant(const QString &nick);
    void applySubject(const QString &text);
    void applyMyNick(const QString &nick);
    void applyJoined(bool joined);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void filterChanged();
    void subjectChanged();
    void meChanged();
    void joinedChanged();
    void countChanged();
    // `action` is what was being attempted, in the words the page will show.
    void actionFailed(const QString &action, const QString &message);

private:
    struct Occupant {
        QString nick;
        QString realJid;
        QString role;
        QString affiliation;
        QString show;
        QString status;
        QVariantMap caps;

        QString group() const;
        bool sameAs(const Occupant &other) const;
    };

    static Occupant fromMap(const QVariantMap &m);
    // Role-then-nick order: the grouping the page draws, so rows arrive in the
    // order they are shown and a heading never appears twice.
    static int compare(const Occupant &a, const Occupant &b);
    static bool less(const Occupant &a, const Occupant &b) {
        return compare(a, b) < 0;
    }
    static bool matches(const Occupant &o, const QString &filter);
    // Where one occupant's avatar lives: the room JID with their nick as the
    // resource. Empty unless both halves are known - "room@svc/" and "/nick"
    // are not JIDs, and asking tacky about one is an error, not a miss.
    QString occupantJid(const QString &nick) const;
    // Forget whoever holds `nick`, answering whether anyone did.
    bool dropNick(const QString &nick);

    // Everything the room holds, in display order. The rows below are this
    // narrowed by `filter`.
    QList<Occupant> m_occupants;
    QList<Occupant> m_rows;

    // Re-derives the rows from m_occupants and walks them onto the model, so a
    // presence moves one row rather than resetting the list under the scroll.
    void rebuild();
    void clearRoom();
    void sendAction(const QString &label, const QString &method,
                    QVariantMap args);
    void notifyRoom(const QString &method, QVariantMap args);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;     // as given, e.g. room@muc.example.com?join
    QString m_roomJid; // what the muc module answers to
    QString m_filter;
    QString m_subject;
    QString m_myNick;
    bool m_joined = false;

    int m_occupantsToken = 0;
    int m_subjectToken = 0;
    int m_nickToken = 0;
    int m_joinedToken = 0;
    QHash<int, QString> m_actions; // token -> what it was, for actionFailed
};

#endif // MUCROOMMODEL_H
