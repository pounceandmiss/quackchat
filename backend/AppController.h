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
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include "AccountSettings.h"
#include "AccountsModel.h"
#include "AppSettings.h"
#include "AudioDevices.h"
#include "AvatarController.h"
#include "CallsModel.h"
#include "ChatListModel.h"
#include "ChatSession.h"
#include "NotificationController.h"
#include "StorageController.h"
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
    // The preferences that are the app's rather than an account's; tacky keeps
    // them in one store with no acc on it.
    Q_PROPERTY(AppSettings *settings READ settings CONSTANT)
    // Local storage encryption. Read before anything else at startup: while the
    // store is locked or a migration is pending, tacky has installed no other
    // module, so nothing else here works yet.
    Q_PROPERTY(StorageController *storage READ storage CONSTANT)
    // Where the backend is writing its log, empty while it is writing to
    // stderr - so also whether there is anything to hand over.
    Q_PROPERTY(QString logPath READ logPath NOTIFY logPathChanged)
    // App-wide for the same reason as calls: an alert names a chat, and which
    // window ends up showing it is decided when the user picks it.
    Q_PROPERTY(NotificationController *notifications READ notifications CONSTANT)
    // True when the backend never came up at all: on the desktop, an
    // interpreter that would not create. Nothing retries that, so the UI says
    // so rather than showing it as merely disconnected.
    Q_PROPERTY(bool backendStartFailed READ backendStartFailed NOTIFY
                   backendStartFailedChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    TackyBackend *backend() { return &m_backend; }
    AccountsModel *accounts() { return &m_accounts; }
    AvatarController *avatars() { return &m_avatars; }
    CallsModel *calls() { return &m_calls; }
    AudioDevices *audio() { return &m_audio; }
    AppSettings *settings() { return &m_settings; }
    StorageController *storage() { return &m_storage; }
    QString logPath() const { return m_logPath; }
    NotificationController *notifications() { return &m_notifications; }
    bool backendStartFailed() const { return m_startFailed; }

    // Set by the GUI host, which owns the QImage side. Not owned here, and
    // expected to outlive this object.
    void setAvatarEncoder(const AvatarEncoder *encoder);

    // Lazily created and cached here; all windows on the same account share one
    // instance. Returns nullptr for an empty acc.
    Q_INVOKABLE ChatListModel *chatListFor(const QString &acc);
    Q_INVOKABLE AccountSettings *accountSettingsFor(const QString &acc);

    // The shared session for one conversation, so every window showing it reads
    // the same history window and composes into the same draft. `groupchat` is
    // applied every call, not only when the session is built: a room held as a
    // 1:1 would offer OMEMO it cannot do. Returns nullptr if either half of the
    // key is empty, which is how a window with no chat open asks.
    //
    // Sessions are kept for the life of the account, not evicted by age: a
    // long-lived window that visits many chats holds the scrollback of each.
    Q_INVOKABLE ChatSession *chatFor(const QString &acc, const QString &jid,
                                     bool groupchat = false);

    // Let go of one conversation's session, so the next chatFor() builds it
    // afresh. Only safe once nothing is showing the chat: every view of it holds
    // the session this drops.
    Q_INVOKABLE void forgetChat(const QString &acc, const QString &jid);

    // The directory holding the log, for a desktop file manager. Qt has no
    // "reveal this file", so the folder is as close as it gets.
    Q_INVOKABLE QUrl logFolder() const;

    // Offer the log to the share chooser. Android only, where the file is in
    // storage nothing else can reach; elsewhere logFolder() is the way to it.
    Q_INVOKABLE void shareLog();

    // What the command line asked of the logger, applied when the backend
    // starts. Each flag owns its own setting for the run: where one is given
    // the stored preference is left alone, which is the rule the Tk client
    // follows too.
    struct DebugArgs {
        QString level;
        QString file;
        QString libdatachannelLevel;
        QString rtcmaLevel;
    };
    void setDebugArgs(const DebugArgs &args);

    // Start a persistent on-disk backend and sign in TACKY_ACC if it is set.
    // No-op once started.
    Q_INVOKABLE void startFromEnvironment();

signals:
    void logPathChanged();
    void backendStartFailedChanged();
    // A backend failure with no request behind it (error <Background>). No
    // model listens for those, so the app itself is what says them. The
    // message only: the event's errorinfo is a Tcl trace for the log.
    void backendError(const QString &message);

private:
    // Drop what was cached for an account that has been removed.
    void forget(const QString &acc);
    // Everything that has to wait for `storage status`. While the store is
    // locked or a migration is pending, tacky has installed neither `log` nor
    // `account` nor `setting`, so asking any of them is an error - and the log
    // calls below are notifies, whose errors have no token to land on and come
    // back as a background failure the whole app reports.
    void applyStoredPreferences();
    // Push the stored logging preferences at the backend. Re-sent on every
    // connect: the settings are stored, but what they drive is per process.
    void applyLogToFile();
    void applyLogLevel();
    void applyLogNative();
    void onResult(int token, const QVariant &data);
    void onEvent(const QString &module, const QString &name, const QVariant &args);

    TackyBackend m_backend;
    AccountsModel m_accounts;
    AvatarController m_avatars;
    CallsModel m_calls;
    AudioDevices m_audio;
    AppSettings m_settings;
    StorageController m_storage;
    NotificationController m_notifications;
    const AvatarEncoder *m_encoder = nullptr;
    DebugArgs m_debug;
    QString m_logPath;
    int m_logPathToken = -1;
    QHash<QString, ChatListModel *> m_chatLists;
    QHash<QString, AccountSettings *> m_accountSettings;
    // acc -> jid -> session, so dropping an account drops its chats with it.
    QHash<QString, QHash<QString, ChatSession *>> m_chatSessions;
    bool m_started = false;
    bool m_startFailed = false;
};

#endif // APPCONTROLLER_H
