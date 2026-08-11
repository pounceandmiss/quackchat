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

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

    void applyList(const QVariantList &entries); // full reset (get / <Changed>)
    void applyItem(const QVariantMap &entry);    // upsert one entry
    void applyRemove(const QString &jid);        // drop one entry

signals:
    void backendChanged();
    void accountChanged();

private:
    // Newest activity first, then name (ci), then jid - total and stable.
    static bool lessThan(const QVariantMap &a, const QVariantMap &b);
    int indexOfJid(const QString &jid) const;
    int insertPos(const QVariantMap &entry) const;

    TackyBackend *m_backend = nullptr;
    QString m_account;
    int m_getToken = -1;
    QList<QVariantMap> m_items;
};

#endif // CHATLISTMODEL_H
