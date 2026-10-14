// The editable side of one account: its stored password and its published
// nickname, plus the OMEMO devices model for the same account. Created and
// cached by AppController, so every window editing an account shares one.
//
// Saving a password writes only the stored login credential, with
// `account set`. tacky hands it to a running client at once: an enabled
// account that is offline (after a failed sign-in, say) tries again with it,
// and one that is online keeps its session. Changing the password on the
// server is changeServerPassword(), which updates the stored one as well.
//
// The avatar is not part of that save: publishing is a server round-trip with
// its own progress, so it is applied the moment a picture is picked, like the
// OMEMO trust controls.
//
// The route (proxy and tor) is saved with the rest, but last: tacky starts
// the account over when it changes, which would fail a nickname write still
// out on the old session. A malformed proxy is refused, and the refusal is the
// status.
//
// What the account tells its contacts - its local time, how long it has been
// away, read receipts - is not part of the save either: each is a setting in
// the account's own store, written the moment it is toggled.
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
    // socks5://[user:pass@]host:port, or empty to connect directly.
    // With tor set, where Tor's SOCKS port is, empty for 127.0.0.1:9050.
    Q_PROPERTY(QString proxy READ proxy NOTIFY proxyChanged)
    // Whether the account goes through Tor, on circuits of its own.
    Q_PROPERTY(bool tor READ tor NOTIFY proxyChanged)
    // Why tacky refused the last proxy saved, in full: it is shown under the
    // field, where the one-line status would cut it short. Cleared by the next
    // save.
    Q_PROPERTY(QString proxyError READ proxyError NOTIFY proxyErrorChanged)
    // Last outcome of a save, for the line under the form. Cleared on the next
    // save; the page times it out.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusError READ statusError NOTIFY statusChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)
    // A server-side change (XEP-0077) in flight, and why the last one failed.
    // Apart from status: the dialog that asked for it is what shows them.
    Q_PROPERTY(bool changingPassword READ changingPassword NOTIFY changingPasswordChanged)
    Q_PROPERTY(QString changePasswordError READ changePasswordError
                   NOTIFY changePasswordErrorChanged)
    Q_PROPERTY(OmemoDevicesModel *devices READ devices CONSTANT)
    // The avatar reports separately: it is applied on its own, and tacky
    // narrates the upload through `avatar <Progress>`.
    Q_PROPERTY(QString avatarStatus READ avatarStatus NOTIFY avatarStatusChanged)
    Q_PROPERTY(bool avatarError READ avatarError NOTIFY avatarStatusChanged)
    Q_PROPERTY(bool avatarBusy READ avatarBusy NOTIFY avatarBusyChanged)
    // Whether contacts may ask this account's local time (XEP-0202) and how
    // long the app has been idle (XEP-0012). Off unless turned on, and
    // answered only to contacts with a subscription to us. tacky's
    // per-account answer_time and answer_last_activity.
    Q_PROPERTY(bool answerTime READ answerTime NOTIFY answerTimeChanged)
    Q_PROPERTY(bool answerLastActivity READ answerLastActivity
                   NOTIFY answerLastActivityChanged)
    // Whether contacts are told when their messages arrive and are read
    // (XEP-0184 receipts, XEP-0333 markers). On unless turned off, and only
    // ever sent to contacts with a subscription to us: tacky's per-account
    // send_chat_markers, the Tk client's "Send read receipts".
    Q_PROPERTY(bool sendChatMarkers READ sendChatMarkers
                   NOTIFY sendChatMarkersChanged)

public:
    explicit AccountSettings(QObject *parent = nullptr);

    QString account() const { return m_account; }
    void setAccount(const QString &acc);

    void setBackend(TackyBackend *backend);

    QString password() const { return m_password; }
    QString nick() const { return m_nick; }
    QString proxy() const { return m_proxy; }
    bool tor() const { return m_tor; }
    QString proxyError() const { return m_proxyError; }
    QString status() const { return m_status; }
    bool statusError() const { return m_statusError; }
    bool saving() const { return m_nickToken >= 0 || m_proxyToken >= 0; }
    bool changingPassword() const { return m_changePasswordToken >= 0; }
    QString changePasswordError() const { return m_changePasswordError; }
    OmemoDevicesModel *devices() { return &m_devices; }

    QString avatarStatus() const { return m_avatarStatus; }
    bool avatarError() const { return m_avatarError; }
    bool avatarBusy() const { return m_avatarToken >= 0; }

    bool answerTime() const { return m_answerTime; }
    bool answerLastActivity() const { return m_answerLastActivity; }
    bool sendChatMarkers() const { return m_sendChatMarkers; }

    // The QImage side of picture handling, owned by the GUI. Without one,
    // setAvatar() reports that it cannot read pictures rather than crashing.
    void setAvatarEncoder(const AvatarEncoder *encoder) { m_encoder = encoder; }

    // (Re)read the stored password and the cached nickname.
    Q_INVOKABLE void refresh();

    // Writes whatever differs from what was loaded. saved() follows once the
    // writes are through, which for the nickname means the server said yes.
    Q_INVOKABLE void save(const QString &password, const QString &nick,
                          const QString &proxy, bool tor);

    // Change the password on the server. On success tacky stores the new one
    // too; serverPasswordChanged() follows, and a failure lands in
    // changePasswordError. resetChangePassword() clears that for a fresh ask.
    Q_INVOKABLE void changeServerPassword(const QString &password);
    Q_INVOKABLE void resetChangePassword();

    // Encode `source` and publish it as this account's avatar, or withdraw the
    // published one. Both take effect immediately; avatarBusy covers the wait.
    Q_INVOKABLE void setAvatar(const QUrl &source);
    Q_INVOKABLE void clearAvatar();

    // Written at once, to this account's own setting store.
    Q_INVOKABLE void setAnswerTime(bool on);
    Q_INVOKABLE void setAnswerLastActivity(bool on);
    Q_INVOKABLE void setSendChatMarkers(bool on);

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
    void proxyChanged();
    void proxyErrorChanged();
    void statusChanged();
    void savingChanged();
    void saved();
    void changingPasswordChanged();
    void changePasswordErrorChanged();
    void serverPasswordChanged();
    void avatarStatusChanged();
    void avatarBusyChanged();
    void answerTimeChanged();
    void answerLastActivityChanged();
    void sendChatMarkersChanged();

private:
    void requestNick();
    void saveProxy(bool wrote);
    void setProxyError(const QString &error);
    void setStatus(const QString &text, bool error);
    void setAvatarStatus(const QString &text, bool error);
    void finishAvatar(const QString &error);
    void setChangePasswordError(const QString &error);
    void requestSettings();
    void applySetting(const QString &key, const QString &value);
    void writeSetting(const QString &key, bool on);

    TackyBackend *m_backend = nullptr;
    const AvatarEncoder *m_encoder = nullptr;
    QString m_account;
    QString m_password;
    QString m_nick;
    QString m_proxy;
    QString m_proxyError;
    bool m_tor = false;
    QString m_pendingProxy; // what save() was given, written after the nick
    bool m_pendingTor = false;
    QString m_status;
    bool m_statusError = false;
    int m_getToken = -1;
    int m_nickGetToken = -1;
    int m_nickToken = -1; // in flight `nick set`
    int m_proxyToken = -1; // in flight `account set` of the route
    int m_avatarToken = -1; // in flight `avatar publish` / `avatar disable`
    int m_changePasswordToken = -1; // in flight `account changePassword`
    QString m_changePasswordError;
    QString m_avatarStatus;
    bool m_avatarError = false;
    // tacky's defaults, in force until a stored value replaces them.
    bool m_answerTime = false;
    bool m_answerLastActivity = false;
    bool m_sendChatMarkers = true;
    int m_answerTimeToken = -1;
    int m_answerLastActivityToken = -1;
    int m_sendChatMarkersToken = -1;
    OmemoDevicesModel m_devices;
};

#endif // ACCOUNTSETTINGS_H
