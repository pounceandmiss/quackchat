#include "AppSettings.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

namespace {
// tacky reads this one itself, at startup.
const QLatin1String kMediaBackend("media_backend");

// "" is a value here, not a missing one: it leaves the choice to tacky.
bool isMediaBackend(const QString &name) {
    return name.isEmpty() || name == QLatin1String("rtc")
           || name == QLatin1String("webrtc");
}

const QLatin1String kAutofetch("attachment_autofetch");
const QLatin1String kAutofetchMax("attachment_autofetch_max");
const QLatin1String kLogToFile("log_to_file");
const QLatin1String kLogLevel("log_level");
const QLatin1String kLogNative("log_native");
const QLatin1String kChatAvatars("chat_avatars");
} // namespace

AppSettings::AppSettings(QObject *parent) : QObject(parent) {}

void AppSettings::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &AppSettings::refresh);
}

void AppSettings::refresh() {
    if (!m_backend)
        return;
    m_autofetchToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kAutofetch}});
    m_autofetchMaxToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kAutofetchMax}});
    m_logToFileToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kLogToFile}});
    m_logLevelToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kLogLevel}});
    m_logNativeToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kLogNative}});
    m_chatAvatarsToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kChatAvatars}});
    m_mediaBackendToken =
        m_backend->request(QStringLiteral("setting"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("key"), kMediaBackend}});
    // Not a setting: what the setting resolved to, which only the backend
    // knows and only after it has tried to open one.
    m_activeBackendToken = m_backend->request(
        QStringLiteral("media"), QStringLiteral("backend"), QVariantMap{});
}

void AppSettings::handleResult(int token, const QVariant &data) {
    if (token == m_autofetchToken)
        applyValue(kAutofetch, data.toString());
    else if (token == m_autofetchMaxToken)
        applyValue(kAutofetchMax, data.toString());
    else if (token == m_logToFileToken)
        applyValue(kLogToFile, data.toString());
    else if (token == m_logLevelToken)
        applyValue(kLogLevel, data.toString());
    else if (token == m_logNativeToken)
        applyValue(kLogNative, data.toString());
    else if (token == m_chatAvatarsToken)
        applyValue(kChatAvatars, data.toString());
    else if (token == m_mediaBackendToken)
        applyValue(kMediaBackend, data.toString());
    else if (token == m_activeBackendToken) {
        const QString name = data.toString();
        if (name == m_activeMediaBackend)
            return;
        m_activeMediaBackend = name;
        emit activeMediaBackendChanged();
    }
}

// The stored value never came, so the compiled-in default stands. Dropping the
// token keeps a late reply from landing on a question already asked again.
void AppSettings::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token == m_autofetchToken)
        m_autofetchToken = -1;
    else if (token == m_autofetchMaxToken)
        m_autofetchMaxToken = -1;
    else if (token == m_logToFileToken)
        m_logToFileToken = -1;
    else if (token == m_logLevelToken)
        m_logLevelToken = -1;
    else if (token == m_logNativeToken)
        m_logNativeToken = -1;
    else if (token == m_chatAvatarsToken)
        m_chatAvatarsToken = -1;
    else if (token == m_mediaBackendToken)
        m_mediaBackendToken = -1;
    else if (token == m_activeBackendToken)
        m_activeBackendToken = -1;
}

void AppSettings::handleEvent(const QString &module, const QString &name,
                              const QVariant &args) {
    // Global, so there is no acc to filter on; applyValue ignores the keys
    // this does not hold.
    if (module != QLatin1String("setting") || name != QLatin1String("Changed"))
        return;
    const QVariantMap a = args.toMap();
    applyValue(a.value(QStringLiteral("key")).toString(),
               a.value(QStringLiteral("value")).toString());
}

// An empty value is a key never written, which leaves tacky's default in force
// - so it leaves ours alone too.
void AppSettings::applyValue(const QString &key, const QString &value) {
    // Before the guard below: an empty media backend is the automatic one,
    // and has to be able to travel like any other value.
    if (key == kMediaBackend) {
        if (m_mediaBackend == value)
            return;
        m_mediaBackend = value;
        emit mediaBackendChanged();
        return;
    }
    if (value.isEmpty())
        return;
    if (key == kAutofetch) {
        if (m_autofetch == value)
            return;
        m_autofetch = value;
        emit attachmentAutofetchChanged();
    } else if (key == kAutofetchMax) {
        const qlonglong bytes = value.toLongLong();
        if (m_autofetchMax == bytes)
            return;
        m_autofetchMax = bytes;
        emit attachmentAutofetchMaxChanged();
    } else if (key == kLogToFile) {
        const bool on = value != QLatin1String("0");
        if (m_logToFile == on)
            return;
        m_logToFile = on;
        emit logToFileChanged();
    } else if (key == kLogLevel) {
        if (m_logLevel == value)
            return;
        m_logLevel = value;
        emit logLevelChanged();
    } else if (key == kLogNative) {
        const bool on = value != QLatin1String("0");
        if (m_logNative == on)
            return;
        m_logNative = on;
        emit logNativeChanged();
    } else if (key == kChatAvatars) {
        const bool on = value != QLatin1String("0");
        if (m_chatAvatars == on)
            return;
        m_chatAvatars = on;
        emit chatAvatarsChanged();
    }
}

void AppSettings::write(const QString &key, const QString &value) {
    if (!m_backend)
        return;
    m_backend->notify(QStringLiteral("setting"), QStringLiteral("set"),
                      QVariantMap{{QStringLiteral("key"), key},
                                  {QStringLiteral("value"), value}});
    applyValue(key, value); // <Changed> confirms; don't wait to redraw
}

void AppSettings::setAttachmentAutofetch(const QString &policy) {
    write(kAutofetch, policy);
}

void AppSettings::setAttachmentAutofetchMax(qlonglong bytes) {
    write(kAutofetchMax, QString::number(bytes));
}

void AppSettings::setLogToFile(bool on) {
    write(kLogToFile, on ? QStringLiteral("1") : QStringLiteral("0"));
}

void AppSettings::setLogLevel(const QString &level) {
    write(kLogLevel, level);
}

void AppSettings::setLogNative(bool on) {
    write(kLogNative, on ? QStringLiteral("1") : QStringLiteral("0"));
}

void AppSettings::setChatAvatars(bool on) {
    write(kChatAvatars, on ? QStringLiteral("1") : QStringLiteral("0"));
}

// Only what this build offers, plus "" for automatic: tacky would accept any
// name and spend a start falling back from it.
void AppSettings::setMediaBackend(const QString &name) {
    if (!isMediaBackend(name))
        return;
    write(kMediaBackend, name);
}
