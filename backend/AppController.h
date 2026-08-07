// The app-wide root, exposed to QML as the `App` singleton. Owns the one
// backend thread plus the shared state (accounts, avatars, a ChatListModel per
// account); windows are just views over it.
//
// startFromEnvironment() is called from main() and never from QML, so loading
// the UI in tests starts no backend thread and touches no disk.
#ifndef APPCONTROLLER_H
#define APPCONTROLLER_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "AccountSettings.h"
#include "AccountsModel.h"
#include "AudioDevices.h"
#include "AvatarController.h"
#include "CallsModel.h"
#include "ChatListModel.h"
#include "NotificationController.h"
#include "TackyBackend.h"

class AppController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON
    Q_PROPERTY(TackyBackend *backend READ backend CONSTANT)
    Q_PROPERTY(AccountsModel *accounts READ accounts CONSTANT)
    Q_PROPERTY(AvatarController *avatars READ avatars CONSTANT)
    // App-wide, not per-window: a call outlives the view that started it, and
    // there is no way to re-enumerate one from the backend.
    Q_PROPERTY(CallsModel *calls READ calls CONSTANT)
    Q_PROPERTY(AudioDevices *audio READ audio CONSTANT)
    // App-wide for the same reason as calls: an alert names a chat, and which
    // window ends up showing it is decided when the user picks it.
    Q_PROPERTY(NotificationController *notifications READ notifications CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);

    TackyBackend *backend() { return &m_backend; }
    AccountsModel *accounts() { return &m_accounts; }
    AvatarController *avatars() { return &m_avatars; }
    CallsModel *calls() { return &m_calls; }
    AudioDevices *audio() { return &m_audio; }
    NotificationController *notifications() { return &m_notifications; }

    // Set by the GUI host, which owns the QImage side. Not owned here, and
    // expected to outlive this object.
    void setAvatarEncoder(const AvatarEncoder *encoder);

    // Lazily created and cached here; all windows on the same account share one
    // instance. Returns nullptr for an empty acc.
    Q_INVOKABLE ChatListModel *chatListFor(const QString &acc);
    Q_INVOKABLE AccountSettings *accountSettingsFor(const QString &acc);

    // Start a persistent on-disk backend and sign in TACKY_ACC if it is set.
    // No-op once started.
    Q_INVOKABLE void startFromEnvironment();

private:
    // Drop what was cached for an account that has been removed.
    void forget(const QString &acc);

    TackyBackend m_backend;
    AccountsModel m_accounts;
    AvatarController m_avatars;
    CallsModel m_calls;
    AudioDevices m_audio;
    NotificationController m_notifications;
    const AvatarEncoder *m_encoder = nullptr;
    QHash<QString, ChatListModel *> m_chatLists;
    QHash<QString, AccountSettings *> m_accountSettings;
    bool m_started = false;
};

#endif // APPCONTROLLER_H
