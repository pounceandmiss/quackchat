// The public rooms a MUC service will admit to, from `muc discoverRooms`, plus
// the default nick a new bookmark would get. Both are what the join-room dialog
// needs beyond the bookmark write itself.
//
// One discovery at a time, in server order: pressing Discover again replaces the
// list rather than adding to it, so a slow first answer must not land on top of
// a second one. tacky decides what a service returns and in what order; this
// only holds it.
#ifndef MUCROOMSMODEL_H
#define MUCROOMSMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class MucRoomsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // A discovery has finished, so an empty list means "this service lists no
    // rooms" rather than "not asked yet".
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // What tacky would nick us if a bookmark named nobody. Fetched on demand;
    // empty until it answers.
    Q_PROPERTY(QString defaultNick READ defaultNick NOTIFY defaultNickChanged)

public:
    enum Role {
        JidRole = Qt::UserRole + 1,
        NameRole,
        OccupantsRole,
    };
    Q_ENUM(Role)

    explicit MucRoomsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    bool loading() const { return m_token != 0; }
    bool loaded() const { return m_loaded; }
    QString error() const { return m_error; }
    QString defaultNick() const { return m_defaultNick; }

    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);

    // Ask `service` (a MUC domain, e.g. conference.example.com) what it hosts.
    Q_INVOKABLE void discover(const QString &service);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void requestDefaultNick();

    // Routing and transforms are public so tests can drive them with canned data.
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);
    void applyRooms(const QVariantList &rooms);

signals:
    void backendChanged();
    void accountChanged();
    void loadingChanged();
    void loadedChanged();
    void errorChanged();
    void defaultNickChanged();

private:
    void setError(const QString &message);
    void setDefaultNick(const QString &nick);
    void setLoaded(bool v);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_error;
    QString m_defaultNick;
    bool m_loaded = false;
    int m_token = 0;     // in-flight discoverRooms, 0 for none
    int m_nickToken = 0; // in-flight bookmarks defaultNick

    QList<QVariantMap> m_rooms;
};

#endif // MUCROOMSMODEL_H
