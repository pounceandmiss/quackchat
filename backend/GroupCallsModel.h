// Every chat's group call this app has heard of, one GroupCall each, exposed
// to QML as `App.groupCalls`. App-lifetime for the reason CallsModel is: the
// backend's state is events, and a window is only a view of them.
//
// Rows appear for a room the moment a chat page asks after it, or the backend
// mentions it, and never go: a room with no call is a row whose GroupCall says
// so. The window manager instantiates over this and shows a window for the
// rows whose phase is not idle.
//
// This owns the wire's fan-out and nothing else. groupcall events go to the
// GroupCall of the chat they name, or else of the call room they name (a
// call's room is the chat's own for an in-room call); the `calls` events of a
// leg go to whichever GroupCall holds that sid, which is how a leg stays out
// of CallsModel.
#ifndef GROUPCALLSMODEL_H
#define GROUPCALLSMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "GroupCall.h"

class TackyBackend;

class GroupCallsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        CallRole = Qt::UserRole + 1,
        AccountRole,
        JidRole,
    };
    Q_ENUM(Role)

    explicit GroupCallsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBackend(TackyBackend *backend);

    // The call of one room, made on first ask. `jid` may be the chat list's
    // form, ?join suffix and all. Returns nullptr for an empty half.
    Q_INVOKABLE GroupCall *callFor(const QString &acc, const QString &jid);
    // Whether a row exists, without making one.
    GroupCall *find(const QString &acc, const QString &jid) const;
    // The row whose call is held in `room`, if any.
    GroupCall *findByCall(const QString &acc, const QString &room) const;

    // Ask what `acc` is in, and re-ask every known room its status. Driven by
    // the account's conn events; exposed so a view can force one.
    Q_INVOKABLE void refreshFor(const QString &acc);

    // Public so tests can drive it with canned events.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

    static QString roomJid(const QString &jid);

signals:
    void countChanged();
    // Someone calling a chat of ours, live: the moment to ring. The invite is
    // stored at `timestamp` in `chat` (its message's chat JID), naming the
    // call's `room`; answer it through callFor(account, chat).
    void invited(const QString &account, const QString &chat, qlonglong timestamp,
                 const QString &room, const QString &from, bool video);

private:
    struct PendingList {
        QString acc;
    };
    TackyBackend *m_backend = nullptr;
    QList<GroupCall *> m_calls;
    QHash<int, PendingList> m_listTokens;
};

#endif // GROUPCALLSMODEL_H
