#include "AppSettings.h"

#include "TackyBackend.h"

namespace {
const QLatin1String kAutofetch("attachment_autofetch");
const QLatin1String kAutofetchMax("attachment_autofetch_max");
} // namespace

AppSettings::AppSettings(QObject *parent) : QObject(parent) {}

void AppSettings::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::event, this, &AppSettings::handleEvent);
        connect(m_backend, &TackyBackend::result, this, &AppSettings::handleResult);
    }
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
}

void AppSettings::handleResult(int token, const QVariant &data) {
    if (token == m_autofetchToken)
        applyValue(kAutofetch, data.toString());
    else if (token == m_autofetchMaxToken)
        applyValue(kAutofetchMax, data.toString());
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
