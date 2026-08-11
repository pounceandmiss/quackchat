// The app-wide account list: one row per account with its JID, enabled flag and
// connection state. Seeded from `account list` and kept live by the account and
// conn events.
#ifndef ACCOUNTSMODEL_H
#define ACCOUNTSMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class AccountsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int connRev READ connRev NOTIFY connRevChanged)

public:
    enum Role {
        JidRole = Qt::UserRole + 1,
        ConnStateRole, // "", connecting, connected, waiting, auth-error, ...
        EnabledRole,
    };
    Q_ENUM(Role)

    explicit AccountsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    void setBackend(TackyBackend *backend);

    // (Re)enumerate accounts from the backend (all + the enabled subset).
    Q_INVOKABLE void refresh();

    // Bumped on every conn-state change. connStateFor() is a plain call, so a
    // QML binding on it has nothing to re-evaluate against; reading this in the
    // same binding gives it the dependency.
    int connRev() const { return m_connRev; }

    // Per-JID lookups; the rail keys everything off the JID.
    Q_INVOKABLE QString connStateFor(const QString &jid) const;
    Q_INVOKABLE bool isEnabled(const QString &jid) const;
    Q_INVOKABLE bool contains(const QString &jid) const { return indexOfJid(jid) >= 0; }
    Q_INVOKABLE QString firstJid() const;

    // Fire-and-forget; the model updates itself off the resulting events.
    // `add` also enables the account, which is what signs it in.
    Q_INVOKABLE void add(const QString &jid, const QString &password);
    Q_INVOKABLE void enable(const QString &jid);
    Q_INVOKABLE void disable(const QString &jid);
    Q_INVOKABLE void remove(const QString &jid);

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

    void applyList(const QVariantList &jids);        // full set (account list)
    void applyEnabledList(const QVariantList &jids); // enabled subset
    void applyAdded(const QString &jid);
    void applyRemoved(const QString &jid);
    void setEnabled(const QString &jid, bool enabled);
    void setConnState(const QString &jid, const QString &state);

signals:
    void backendChanged();
    void countChanged();
    void connRevChanged();

private:
    // Asks tacky to re-fire this account's conn events at their current value,
    // for rows built from `account list` rather than from a live transition.
    void pullConnState(const QString &jid);

    int m_connRev = 0;

    struct Account {
        QString jid;
        QString connState;
        bool enabled = false;
    };

    int indexOfJid(const QString &jid) const;
    int insertPos(const QString &jid) const; // rows stay sorted by JID (ci)

    TackyBackend *m_backend = nullptr;
    int m_listToken = -1;
    int m_enabledToken = -1;
    QSet<QString> m_enabledJids;
    QList<Account> m_accounts;
};

#endif // ACCOUNTSMODEL_H
