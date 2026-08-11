// The per-account chat list (roster + bookmarks + history), from `chatlist get`
// and the chatlist events. The backend keeps it unordered; we sort by last
// activity so it reads like a conversations list.
#ifndef CHATLISTMODEL_H
#define CHATLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class ChatListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged)

public:
    enum Role {
        JidRole = Qt::UserRole + 1,
        NameRole,
        SourceRole,
        GroupchatRole,
        AutojoinRole,
        LastActivityRole,
        SubscriptionRole,
        RoomStateRole,
        RoomReasonRole,
        UnreadRole,
        UnreadMentionsRole,
        RawRole,
    };
    Q_ENUM(Role)

    explicit ChatListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString account() const { return m_account; }
    void setAccount(const QString &acc);

    TackyBackend *backend() const { return m_backend; }
    void setBackend(TackyBackend *backend);

    // (Re)load the whole list for the current account.
    Q_INVOKABLE void refresh();

    // The chat_entry for one JID, or an empty map when there is no such chat.
    Q_INVOKABLE QVariantMap entryFor(const QString &jid) const;

    // Roster and bookmark edits: what the list's context menu and its new-chat
    // and join-room dialogs do. Pass-throughs, like the Tk list's menu handlers.
    // tacky owns the rules - including cutting a room's `?join` chat JID back to
    // the bare room JID a bookmark is keyed by - so the JID a row carries goes
    // straight in. None of them touch m_items either: the chatlist events the
    // edit provokes are what repaint the list, so a rejected edit leaves no
    // phantom row behind.
    Q_INVOKABLE void addContact(const QString &jid, const QString &name = {});
    Q_INVOKABLE void renameContact(const QString &jid, const QString &name);
    Q_INVOKABLE void removeContact(const QString &jid);
    // Membership, not attendance: joining a room also bookmarks it with
    // autojoin set, and leaving clears the flag, which is the Tk list's "Join"
    // tick. forceJoinRoom re-sends the join alone, for a room we are a member
    // of but have been dropped from.
    Q_INVOKABLE void joinRoom(const QString &jid, const QString &nick = {},
                              const QString &password = {});
    Q_INVOKABLE void leaveRoom(const QString &jid);
    Q_INVOKABLE void forceJoinRoom(const QString &jid);
    Q_INVOKABLE void renameBookmark(const QString &jid, const QString &name);
    Q_INVOKABLE void removeBookmark(const QString &jid);

    // Re-ask the server for the roster and the bookmarks, then reload. refresh()
    // alone only re-reads what tacky already has stored.
    Q_INVOKABLE void reload();

    // Why the last load failed, or "" if it did not. An empty list and a failed
    // one look identical otherwise, which is how a schema error once presented
    // itself as "no conversations yet".
    QString loadError() const { return m_loadError; }

    // Public so tests can drive it with canned replies.
    void handleError(int token, const QString &message);

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

    void applyList(const QVariantList &entries); // full reset (get / <Changed>)
    void applyItem(const QVariantMap &entry);    // upsert one entry
    void applyRemove(const QString &jid);        // drop one entry

signals:
    void loadErrorChanged();
    void backendChanged();
    void accountChanged();

private:
    // Newest activity first, then display name (ci), then jid - total and
    // stable. Contacts never messaged all share activity 0, so the name leg
    // orders that whole block rather than the odd collision.
    static bool lessThan(const QVariantMap &a, const QVariantMap &b);
    int indexOfJid(const QString &jid) const;
    int insertPos(const QVariantMap &entry) const;

    // One roster/bookmark edit: stamps the account on and sends it, or drops it
    // when there is no account or no JID to act on.
    void sendEdit(const QString &module, const QString &method, QVariantMap args);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    void setLoadError(const QString &message);

    int m_getToken = -1;
    QString m_loadError;
    QList<QVariantMap> m_items;
};

#endif // CHATLISTMODEL_H
