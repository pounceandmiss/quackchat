// One room's people - everyone in it, then the members who are not - and the
// room-level state a details screen draws around them: the subject, who we are
// in there, and whether we are in there at all. Read from `muc people`, and
// read again on each `muc <PeopleChanged>`.
//
// tacky does the deciding: who counts as a person, the grouping and order,
// the counts, `caps` (what this account may do to each, and `me`, to the room
// itself), and each member's OMEMO `keys`. This model only holds the answer,
// narrows it by `filter`, and moves the rows onto the view one at a time.
//
// It reads only while `active`, which the page holds while it is on screen: a
// room's page is built with its chat, and a busy room changes many times a
// minute. A change while inactive is remembered, and read on becoming active.
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
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

    Q_PROPERTY(QString subject READ subject NOTIFY subjectChanged)
    // Our own person, the one tacky marks `self`, for the card that draws us
    // outside the list.
    Q_PROPERTY(QString myNick READ myNick NOTIFY meChanged)
    Q_PROPERTY(QString myOccupantJid READ myOccupantJid NOTIFY meChanged)
    Q_PROPERTY(QString myRole READ myRole NOTIFY meChanged)
    Q_PROPERTY(QString myAffiliation READ myAffiliation NOTIFY meChanged)
    // What we may do about the room itself: request_voice, destroy.
    Q_PROPERTY(QVariantMap me READ me NOTIFY meChanged)
    Q_PROPERTY(bool joined READ joined NOTIFY joinedChanged)
    // How far the member list got: none, pending, complete, partial, presence.
    Q_PROPERTY(QString memberList READ memberList NOTIFY countChanged)

    // Rows after the filter, and people before it - "3 of 48" needs both.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    // group -> how many people are in it, before the filter, as tacky counts
    // them. A section header cannot count its own rows.
    Q_PROPERTY(QVariantMap groupCounts READ groupCounts NOTIFY countChanged)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1, // the same for one person across reads
        NickRole,                   // "" for a member who is not there
        // room@service/nick: the JID their avatar lives under, and the one a
        // private message would go to. "" for someone not there.
        OccupantJidRole,
        // Their own bare JID, which a semi-anonymous room does not disclose.
        RealJidRole,
        RoleRole,        // moderator, participant, visitor, none
        AffiliationRole, // owner, admin, member, none, outcast
        ShowRole,        // "" (available), chat, away, xa, dnd
        StatusRole,      // whatever they typed as their presence text
        CapsRole,        // tacky's moderation flags against this person
        SelfRole,
        // moderator, participant, visitor, other, absent. The wording of the
        // heading is the page's business, not this model's.
        GroupRole,
        PresentRole,
        // Their OMEMO keys in a room that can be encrypted, {} otherwise:
        // {keys, trusted, undecided, untrusted, compromised, attention, reason}.
        KeysRole,
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

    bool active() const { return m_active; }
    void setActive(bool on);

    QString subject() const { return m_subject; }
    QString myNick() const { return mine().nick; }
    QString myOccupantJid() const { return mine().occupantJid; }
    QString myRole() const { return mine().role; }
    QString myAffiliation() const { return mine().affiliation; }
    QVariantMap me() const { return m_me; }
    bool joined() const { return m_joined; }
    QString memberList() const { return m_memberList; }
    int total() const { return m_people.size(); }
    QVariantMap groupCounts() const { return m_groups; }

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

    void applyPeople(const QVariantMap &answer);
    void applySubject(const QString &text);
    void applyJoined(bool joined);

signals:
    void backendChanged();
    void accountChanged();
    void jidChanged();
    void filterChanged();
    void activeChanged();
    void subjectChanged();
    void meChanged();
    void joinedChanged();
    void countChanged();
    // `action` is what was being attempted, in the words the page will show.
    void actionFailed(const QString &action, const QString &message);

private:
    struct Person {
        QString key;
        QString nick;
        QString occupantJid;
        QString realJid;
        QString role;
        QString affiliation;
        QString show;
        QString status;
        QVariantMap caps;
        bool self = false;
        QString group;
        bool present = false;
        QVariantMap keys;

        bool sameAs(const Person &o) const {
            return key == o.key && nick == o.nick && occupantJid == o.occupantJid
                   && realJid == o.realJid && role == o.role
                   && affiliation == o.affiliation && show == o.show
                   && status == o.status && caps == o.caps && self == o.self
                   && group == o.group && present == o.present && keys == o.keys;
        }
    };

    static Person fromMap(const QVariantMap &m);
    static bool matches(const Person &p, const QString &filter);
    // The person tacky marks as us, or an empty one.
    Person mine() const;

    // Everyone, in tacky's order. The rows below are this narrowed by `filter`.
    QList<Person> m_people;
    QList<Person> m_rows;

    // Walks the rows onto the model by key, so a presence moves one row rather
    // than resetting the list under the scroll.
    void rebuild();
    void clearRoom();
    void readPeople();
    void sendAction(const QString &label, const QString &method,
                    QVariantMap args);
    void notifyRoom(const QString &method, QVariantMap args);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_jid;     // as given, e.g. room@muc.example.com?join
    QString m_roomJid; // what the muc module answers to
    QString m_filter;
    QString m_subject;
    QVariantMap m_me;
    QVariantMap m_groups;
    QString m_memberList;
    bool m_joined = false;
    bool m_active = false;
    // A change arrived while inactive, or nothing has been read yet.
    bool m_stale = true;

    int m_peopleToken = 0;
    int m_subjectToken = 0;
    int m_joinedToken = 0;
    QHash<int, QString> m_actions; // token -> what it was, for actionFailed
};

#endif // MUCROOMMODEL_H
