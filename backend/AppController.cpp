#include "AppController.h"

#include "LogBridge.h"
#include "Notifier.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <utility>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>

#include "SocketTransport.h"

// Abstract-namespace name the backend service binds. Same string on both sides;
// SELinux keeps other apps from reaching it, since they cannot connectto a
// socket owned by a different UID.
static const QLatin1String kAndroidBackendSocket("quackchat.backend");
#endif

AppController::AppController(QObject *parent) : QObject(parent) {
    m_accounts.setBackend(&m_backend);
    m_avatars.setBackend(&m_backend);
    m_calls.setBackend(&m_backend);
    m_audio.setBackend(&m_backend);
    m_video.setBackend(&m_backend);
    m_settings.setBackend(&m_backend);
    m_storage.setBackend(&m_backend);
    m_notifications.setBackend(&m_backend);
    // The per-account models are cached for as long as the account is here, and
    // no longer: an account that has been removed has a roster nobody can reach
    // and a backend connection still listening for its events.
    connect(&m_accounts, &AccountsModel::removed, this, &AppController::forget);
    connect(&m_settings, &AppSettings::logToFileChanged, this,
            &AppController::applyLogToFile);
    connect(&m_settings, &AppSettings::logLevelChanged, this,
            &AppController::applyLogLevel);
    connect(&m_settings, &AppSettings::logNativeChanged, this,
            &AppController::applyLogNative);
    // Not on `connected`: a locked store has no `log` module to talk to yet.
    // StorageController is bound to `connected` itself, so its status answer is
    // the later signal and the only one that means the backend is open.
    connect(&m_storage, &StorageController::statusChanged, this,
            &AppController::applyStoredPreferences);
    connect(&m_backend, &TackyBackend::result, this, &AppController::onResult);
    connect(&m_backend, &TackyBackend::event, this, &AppController::onEvent);
}

// The error module carries failures no reply can carry - a timer, a socket
// handler, the stanza loop. Every model filters on its own module, so without
// this they land nowhere at all.
void AppController::onEvent(const QString &module, const QString &,
                            const QVariant &args) {
    if (module != QLatin1String("error"))
        return;
    const QString message =
        args.toMap().value(QStringLiteral("message")).toString();
    if (!message.isEmpty())
        emit backendError(message);
}

void AppController::setDebugArgs(const DebugArgs &args) {
    m_debug = args;
}

// The reads and pushes only a backend past its storage gate can answer. On
// every status change, not just the first: on Android the interpreter lives in
// a service the UI reattaches to, so the link comes back each launch.
//
// Accounts already on disk auto-connect but never re-emit <Added>, so the rail
// only sees them if we enumerate; the audio and app preferences are stored
// rather than announced, so they want asking too.
void AppController::applyStoredPreferences() {
    if (!m_storage.ready())
        return;
    applyLogToFile();
    applyLogLevel();
    applyLogNative();
    m_accounts.refresh();
    m_audio.refresh();
    m_settings.refresh();
}

void AppController::applyLogToFile() {
    if (m_debug.file.isEmpty())
        m_backend.notify(
            QStringLiteral("log"), QStringLiteral("setenabled"),
            QVariantMap{{QStringLiteral("enabled"), m_settings.logToFile()}});
    // Read back rather than worked out here: the backend picks the path, and
    // with --debug-file it is already writing to one of its own.
    m_logPathToken =
        m_backend.request(QStringLiteral("log"), QStringLiteral("getfile"));
}

void AppController::applyLogLevel() {
    if (!m_debug.level.isEmpty())
        return;
    m_backend.notify(
        QStringLiteral("log"), QStringLiteral("setlevel"),
        QVariantMap{{QStringLiteral("level"), m_settings.logLevel()}});
}

// The setting is a switch and the native loggers take a level, so `debug` is
// what "on" means - they do their own filtering, and jlog does not re-filter.
void AppController::applyLogNative() {
    if (!m_debug.libdatachannelLevel.isEmpty() || !m_debug.rtcmaLevel.isEmpty()
        || !m_debug.webrtcLevel.isEmpty() || !m_debug.rtcmvLevel.isEmpty())
        return;
    m_backend.notify(
        QStringLiteral("log"), QStringLiteral("setnativelevel"),
        QVariantMap{{QStringLiteral("level"), m_settings.logNative()
                                                  ? QStringLiteral("debug")
                                                  : QStringLiteral("none")}});
}

void AppController::onResult(int token, const QVariant &data) {
    if (token != m_logPathToken)
        return;
    m_logPathToken = -1;
    const QString path = data.toString();
    if (m_logPath == path)
        return;
    m_logPath = path;
    // Nothing to add to stderr that the handler underneath us has not already
    // printed, so the bridge only runs while there is a file to add it to.
    setLogBridgeActive(!m_logPath.isEmpty());
    emit logPathChanged();
}

QUrl AppController::logFolder() const {
    if (m_logPath.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(m_logPath).absolutePath());
}

void AppController::shareLog() {
    if (m_logPath.isEmpty())
        return;
#ifdef Q_OS_ANDROID
    QJniObject::callStaticMethod<void>(
        "org/qtproject/example/quackchat/LogExport", "share",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        QNativeInterface::QAndroidApplication::context().object(),
        QJniObject::fromString(m_logPath).object<jstring>(),
        QJniObject::fromString(tr("Quack log")).object<jstring>());
#endif
}

void AppController::forget(const QString &acc) {
    // deleteLater rather than delete: a view bound to one of these is still
    // holding it while the removal is being announced.
    if (ChatListModel *m = m_chatLists.take(acc))
        m->deleteLater();
    if (AccountSettings *s = m_accountSettings.take(acc))
        s->deleteLater();
    for (ChatSession *c : std::as_const(m_chatSessions[acc]))
        c->deleteLater();
    m_chatSessions.remove(acc);
}

ChatListModel *AppController::chatListFor(const QString &acc) {
    if (acc.isEmpty())
        return nullptr;
    ChatListModel *&m = m_chatLists[acc];
    if (!m) {
        m = new ChatListModel(this);
        m->setBackend(&m_backend);
        m->setAccount(acc);
    }
    return m;
}

ChatSession *AppController::chatFor(const QString &acc, const QString &jid,
                                    bool groupchat) {
    if (acc.isEmpty() || jid.isEmpty())
        return nullptr;
    ChatSession *&c = m_chatSessions[acc][jid];
    if (!c)
        c = new ChatSession(&m_backend, acc, jid, groupchat, this);
    else
        c->setGroupchat(groupchat);
    return c;
}

void AppController::forgetChat(const QString &acc, const QString &jid) {
    auto it = m_chatSessions.find(acc);
    if (it == m_chatSessions.end())
        return;
    if (ChatSession *c = it->take(jid))
        c->deleteLater();
}

AccountSettings *AppController::accountSettingsFor(const QString &acc) {
    if (acc.isEmpty())
        return nullptr;
    AccountSettings *&s = m_accountSettings[acc];
    if (!s) {
        s = new AccountSettings(this);
        s->setAvatarEncoder(m_encoder);
        s->setBackend(&m_backend);
        s->setAccount(acc);
    }
    return s;
}

// main() sets this before the UI loads, but a settings page opened first would
// otherwise keep a null encoder for good, so reach the cached ones too.
void AppController::setAvatarEncoder(const AvatarEncoder *encoder) {
    m_encoder = encoder;
    for (AccountSettings *s : std::as_const(m_accountSettings))
        s->setAvatarEncoder(encoder);
}

QStringList AppController::tacoArgs() const {
    return tacoArgs(m_mediaBackendOverride);
}

void AppController::setMediaBackendOverride(const QString &name) {
    if (!name.isEmpty() && name != QLatin1String("rtc")
        && name != QLatin1String("webrtc")) {
        qWarning("unknown media backend %s; using the stored one",
                 qUtf8Printable(name));
        return;
    }
    m_mediaBackendOverride = name;
}

#ifdef Q_OS_WIN
static const char kWebrtcLib[] = "/libtacky_webrtc.dll";
#else
static const char kWebrtcLib[] = "/libtacky_webrtc.so";
#endif

QStringList AppController::tacoArgs(const QString &mediaBackend) const {
    // Persist to disk so an enabled account reconnects next launch without the
    // env vars. No -config-dir override, so we share tacky's own store
    // (~/.config/tacky) rather than keeping a separate quackchat one.
    QStringList args{QStringLiteral("-transient"), QStringLiteral("0")};
    // Here rather than through the log module once it is up: these have to be
    // in force before the backend writes its first line.
    if (!m_debug.level.isEmpty())
        args << QStringLiteral("-debug-level") << m_debug.level;
    if (!m_debug.file.isEmpty())
        args << QStringLiteral("-debug-file") << m_debug.file;
    if (!m_debug.libdatachannelLevel.isEmpty())
        args << QStringLiteral("-libdatachannel-debug-level")
             << m_debug.libdatachannelLevel;
    if (!m_debug.rtcmaLevel.isEmpty())
        args << QStringLiteral("-rtcma-debug-level") << m_debug.rtcmaLevel;
    if (!m_debug.webrtcLevel.isEmpty())
        args << QStringLiteral("-webrtc-debug-level") << m_debug.webrtcLevel;
    if (!m_debug.rtcmvLevel.isEmpty())
        args << QStringLiteral("-rtcmv-debug-level") << m_debug.rtcmvLevel;
    // Which backend runs is tacky's own setting; this only overrides it for
    // the run. The library path goes either way, since the setting is not ours
    // to read - and it is explicit because an embedded interpreter has no
    // executable to look beside.
    if (!mediaBackend.isEmpty())
        args << QStringLiteral("-media-backend") << mediaBackend;
    const QString lib =
        QCoreApplication::applicationDirPath() + QLatin1String(kWebrtcLib);
    if (QFileInfo::exists(lib))
        args << QStringLiteral("-webrtc-lib") << lib;
    return args;
}

void AppController::startFromEnvironment() {
    if (m_started)
        return;
    m_started = true;

    // Here rather than in the constructor: this is the one entry point QML
    // never reaches, so loading the UI in a test opens no session bus. Null on
    // a platform with no implementation, which disables alerts and nothing
    // else.
    m_notifications.setNotifier(createPlatformNotifier());

#ifdef Q_OS_ANDROID
    // The interpreter belongs to the backend service, in a process that
    // outlives this one: Qt exits this process when the activity is destroyed.
    // Starting our own here would put a second writer on tacky's store.
    const auto context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "org/qtproject/example/quackchat/QuackBackendService", "start",
        "(Landroid/content/Context;)V", context.object());
    QJniObject::callStaticMethod<void>(
        "org/qtproject/example/quackchat/Notifications", "requestPermission",
        "(Landroid/content/Context;)V", context.object());
    // Already running is the common case, so the socket is retried rather than
    // sequenced after the service: whoever is ready first waits for the other.
    m_backend.setTransport(new SocketTransport(kAndroidBackendSocket));
    const bool started = m_backend.start();
#else
    bool started = m_backend.start(tacoArgs());
    // A backend the interpreter rejects would stop every start; retry on rtc.
    if (!started && m_mediaBackendOverride != QLatin1String("rtc")) {
        qWarning("tacky would not start; retrying on the rtc media backend");
        started = m_backend.start(tacoArgs(QStringLiteral("rtc")));
    }
#endif

    // Nothing retries this: an interpreter that would not create is not going
    // to on its own, and a socket that is merely down reports itself through
    // the transport instead.
    if (!started) {
        m_startFailed = true;
        emit backendStartFailedChanged();
    }

    const QString acc = qEnvironmentVariable("TACKY_ACC");
    if (!acc.isEmpty()) {
        m_backend.notify(
            QStringLiteral("account"), QStringLiteral("add"),
            QVariantMap{{QStringLiteral("acc"), acc},
                        {QStringLiteral("password"),
                         qEnvironmentVariable("TACKY_PASSWORD")}});
        m_backend.notify(QStringLiteral("account"), QStringLiteral("enable"),
                         QVariantMap{{QStringLiteral("acc"), acc}});
    }

    // Nothing else is asked for here: `storage status` decides when the rest of
    // the backend exists, and applyStoredPreferences does the asking once it
    // answers. StorageController is bound to the connected edge, so on Android,
    // where the socket is still coming up at this point, it re-asks by itself.
}
