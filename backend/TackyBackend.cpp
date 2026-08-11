#include "TackyBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLoggingCategory>

#include "TackyTransport.h"

#ifndef Q_OS_ANDROID
#include "EmbeddedTransport.h"
#endif

// Every JSON message either way, which is the only way to tell a backend
// misbehaving from us misreading it. The level is what keeps it quiet: without
// one, debug output is on and buries the terminal.
//   QT_LOGGING_RULES='quack.*.debug=true' quackchat 2>wire.log
Q_LOGGING_CATEGORY(lcWire, "quack.wire", QtWarningMsg)

TackyBackend::TackyBackend(QObject *parent) : QObject(parent) {}

TackyBackend::~TackyBackend() { stop(); }

bool TackyBackend::isRunning() const {
    return m_transport && m_transport->isConnected();
}

void TackyBackend::setTransport(TackyTransport *transport) {
    if (m_transport == transport)
        return;
    const bool was = isRunning();
    delete m_transport;
    m_transport = transport;
    if (m_transport) {
        m_transport->setParent(this);
        connect(m_transport, &TackyTransport::received, this,
                &TackyBackend::deliver);
        connect(m_transport, &TackyTransport::connectedChanged, this,
                &TackyBackend::onTransportStateChanged);
    }
    if (was != isRunning())
        onTransportStateChanged();
}

void TackyBackend::onTransportStateChanged() {
    if (!isRunning())
        m_inflight.clear(); // those replies are never coming
    emit runningChanged();
    if (isRunning())
        emit connected();
}

bool TackyBackend::start(const QStringList &tacoArgs) {
    if (!m_transport) {
#ifdef Q_OS_ANDROID
        // There is no in-process interpreter to fall back on: the session
        // belongs to the backend service, and a second one here would put two
        // writers on tacky's store. Callers must supply a transport.
        return false;
#else
        setTransport(new EmbeddedTransport);
#endif
    }
    return m_transport->start(tacoArgs);
}

void TackyBackend::stop() {
    if (m_transport)
        m_transport->stop();
}

void TackyBackend::sendArray(const QString &module, const QString &method,
                             const QVariant &args, bool withToken, int token) {
    // Ahead of the guard, so a model's outbound calls stay observable in the
    // tests, which never start an interpreter.
    emit sent(module, method, args.isValid() ? args : QVariantMap());
    qCDebug(lcWire).noquote() << "->" << module << method
                              << (withToken ? QString::number(token)
                                            : QStringLiteral("(notify)"))
                              << args;
    if (!isRunning())
        return;
    QJsonArray arr;
    arr.append(module);
    arr.append(method);
    arr.append(QJsonValue::fromVariant(args.isValid() ? args : QVariantMap()));
    if (withToken) {
        arr.append(token);
        // Kept so an error reply can name what failed. Only for requests that
        // actually went out, so nothing accumulates for calls dropped above.
        m_inflight.insert(token, module + QLatin1Char('/') + method);
    }
    m_transport->send(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

int TackyBackend::request(const QString &module, const QString &method,
                          const QVariant &args) {
    const int token = m_nextToken++;
    sendArray(module, method, args, /*withToken=*/true, token);
    return token;
}

void TackyBackend::notify(const QString &module, const QString &method,
                          const QVariant &args) {
    sendArray(module, method, args, /*withToken=*/false, 0);
}

void TackyBackend::deliver(const QString &json) {
    emit rawMessage(json);
    qCDebug(lcWire).noquote() << "<-" << json;

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray())
        return;
    const QJsonArray arr = doc.array();
    if (arr.isEmpty())
        return;

    const QString tag = arr.at(0).toString();
    if (tag == QLatin1String("result")) {
        const int token = arr.at(1).toInt();
        m_inflight.remove(token);
        emit result(token, arr.at(2).toVariant());
    } else if (tag == QLatin1String("error")) {
        const int token = arr.at(1).toInt();
        const QString message = arr.at(2).toString();
        // Logged here because most callers do not connect error(): a failed
        // request otherwise looks exactly like an empty result, which is how a
        // schema error once read as "no conversations yet".
        qWarning("tacky %s failed: %s",
                 qUtf8Printable(m_inflight.value(token, QStringLiteral("request"))),
                 qUtf8Printable(message));
        m_inflight.remove(token);
        emit error(token, message);
    } else if (tag == QLatin1String("event")) {
        emit event(arr.at(1).toString(), arr.at(2).toString(),
                   arr.at(3).toVariant());
    }
}
