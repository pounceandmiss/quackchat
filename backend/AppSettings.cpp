#include "AppSettings.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

namespace {
const QLatin1String kAutofetch("attachment_autofetch");
const QLatin1String kAutofetchMax("attachment_autofetch_max");
const QLatin1String kLogToFile("log_to_file");
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
}

void AppSettings::handleResult(int token, const QVariant &data) {
    if (token == m_autofetchToken)
        applyValue(kAutofetch, data.toString());
    else if (token == m_autofetchMaxToken)
        applyValue(kAutofetchMax, data.toString());
    else if (token == m_logToFileToken)
        applyValue(kLogToFile, data.toString());
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
