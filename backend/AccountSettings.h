// The editable side of one account: its stored password and its published
// nickname, plus the OMEMO devices model for the same account. Created and
// cached by AppController, so every window editing an account shares one.
//
// Saving a password writes the stored credential with `account add`, which
// updates an account that already exists - the same call tacky's own sign-in
// form uses. It does not reconnect: the running session keeps the old
// credential until the account is disabled and enabled again.
#ifndef ACCOUNTSETTINGS_H
#define ACCOUNTSETTINGS_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "OmemoDevicesModel.h"

class TackyBackend;

class AccountSettings : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString account READ account NOTIFY accountChanged)
    Q_PROPERTY(QString password READ password NOTIFY passwordChanged)
    Q_PROPERTY(QString nick READ nick NOTIFY nickChanged)
    // Last outcome of a save, for the line under the form. Cleared on the next
    // save; the page times it out.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    Q_PROPERTY(OmemoDevicesModel *devices READ devices CONSTANT)

public:
    explicit AccountSettings(QObject *parent = nullptr);

    QString account() const { return m_account; }
    void setAccount(const QString &acc);

    void setBackend(TackyBackend *backend);

    QString password() const { return m_password; }
    QString nick() const { return m_nick; }
    QString status() const { return m_status; }
    bool statusError() const { return m_statusError; }
    bool saving() const { return m_nickToken >= 0; }
    OmemoDevicesModel *devices() { return &m_devices; }

    // (Re)read the stored password and the cached nickname.
    Q_INVOKABLE void refresh();

    // Writes whatever differs from what was loaded. saved() follows once the
    // writes are through, which for the nickname means the server said yes.
    Q_INVOKABLE void save(const QString &password, const QString &nick);

    // Routing and transforms are public so tests can drive them with canned data.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

    void applyAccount(const QVariantMap &row);
    void applyNick(const QString &nick);

signals:
    void accountChanged();
    void passwordChanged();
    void nickChanged();
    void statusChanged();
    void savingChanged();
    void saved();

private:
    void requestNick();
    void setStatus(const QString &text, bool error);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_password;
    QString m_nick;
    QString m_status;
    bool m_statusError = false;
    int m_getToken = -1;
    int m_nickGetToken = -1;
    int m_nickToken = -1; // in flight `nick set`, which is what saving() means
    OmemoDevicesModel m_devices;
};

#endif // ACCOUNTSETTINGS_H
