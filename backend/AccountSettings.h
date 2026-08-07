// The editable side of one account: its stored password and its published
// nickname, plus the OMEMO devices model for the same account. Created and
// cached by AppController, so every window editing an account shares one.
//
// Saving a password writes the stored credential with `account add`, which
// updates an account that already exists - the same call tacky's own sign-in
// form uses. It does not reconnect: the running session keeps the old
// credential until the account is disabled and enabled again.
//
// The avatar is not part of that save: publishing is a server round-trip with
// its own progress, so it is applied the moment a picture is picked, like the
// OMEMO trust controls.
#ifndef ACCOUNTSETTINGS_H
#define ACCOUNTSETTINGS_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "OmemoDevicesModel.h"

class AvatarEncoder;
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
    // The avatar reports separately: it is applied on its own, and tacky
    // narrates the upload through `avatar <Progress>`.
    Q_PROPERTY(QString avatarStatus READ avatarStatus NOTIFY avatarStatusChanged)
    Q_PROPERTY(bool avatarError READ avatarError NOTIFY avatarStatusChanged)
    Q_PROPERTY(bool avatarBusy READ avatarBusy NOTIFY avatarBusyChanged)

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

    QString avatarStatus() const { return m_avatarStatus; }
    bool avatarError() const { return m_avatarError; }
    bool avatarBusy() const { return m_avatarToken >= 0; }

    // The QImage side of picture handling, owned by the GUI. Without one,
    // setAvatar() reports that it cannot read pictures rather than crashing.
    void setAvatarEncoder(const AvatarEncoder *encoder) { m_encoder = encoder; }

    // (Re)read the stored password and the cached nickname.
    Q_INVOKABLE void refresh();

    // Writes whatever differs from what was loaded. saved() follows once the
    // writes are through, which for the nickname means the server said yes.
    Q_INVOKABLE void save(const QString &password, const QString &nick);

    // Encode `source` and publish it as this account's avatar, or withdraw the
    // published one. Both take effect immediately; avatarBusy covers the wait.
    Q_INVOKABLE void setAvatar(const QUrl &source);
    Q_INVOKABLE void clearAvatar();

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
    void avatarStatusChanged();
    void avatarBusyChanged();

private:
    void requestNick();
    void setStatus(const QString &text, bool error);
    void setAvatarStatus(const QString &text, bool error);
    void finishAvatar(const QString &error);

    TackyBackend *m_backend = nullptr;
    const AvatarEncoder *m_encoder = nullptr;
    QString m_account;
    QString m_password;
    QString m_nick;
    QString m_status;
    bool m_statusError = false;
    int m_getToken = -1;
    int m_nickGetToken = -1;
    int m_nickToken = -1; // in flight `nick set`, which is what saving() means
    int m_avatarToken = -1; // in flight `avatar publish` / `avatar disable`
    QString m_avatarStatus;
    bool m_avatarError = false;
    OmemoDevicesModel m_devices;
};

#endif // ACCOUNTSETTINGS_H
