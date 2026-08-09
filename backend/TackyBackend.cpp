#include "TackyBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>

#include <vector>

#include "tacky.h"

TackyBackend::TackyBackend(QObject *parent) : QObject(parent) {}

TackyBackend::~TackyBackend() { stop(); }

void TackyBackend::emitTrampoline(void *ud, const char *json, size_t len) {
    auto *self = static_cast<TackyBackend *>(ud);
    // Copy out of the transient buffer, then hop to the owning thread: the
    // functor form of invokeMethod delivers on `self`'s thread.
    QString s = QString::fromUtf8(json, static_cast<int>(len));
    QMetaObject::invokeMethod(
        self, [self, s]() { self->deliver(s); }, Qt::QueuedConnection);
}

bool TackyBackend::start(const QStringList &tacoArgs) {
    if (m_client)
        return true;

    // Hold the UTF-8 bytes alive across the call (tacky_create strdups them).
    QList<QByteArray> holder;
    holder.reserve(tacoArgs.size());
    std::vector<const char *> argv;
    argv.reserve(tacoArgs.size() + 1);
    for (const QString &a : tacoArgs) {
        holder.append(a.toUtf8());
        argv.push_back(holder.last().constData());
    }
    argv.push_back(nullptr);

    m_client = tacky_create(argv.data(), &TackyBackend::emitTrampoline, this);
    if (m_client)
        emit runningChanged();
    return m_client != nullptr;
}

void TackyBackend::stop() {
    if (!m_client)
        return;
    tacky *c = m_client;
    m_client = nullptr;
    tacky_destroy(c); // no callbacks fire after this returns
    emit runningChanged();
}

void TackyBackend::sendArray(const QString &module, const QString &method,
                             const QVariant &args, bool withToken, int token) {
    // Ahead of the guard, so a model's outbound calls stay observable in the
    // tests, which never start an interpreter.
    emit sent(module, method, args.isValid() ? args : QVariantMap());
    if (!m_client)
        return;
    QJsonArray arr;
    arr.append(module);
    arr.append(method);
    arr.append(QJsonValue::fromVariant(args.isValid() ? args : QVariantMap()));
    if (withToken)
        arr.append(token);
    const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    tacky_send(m_client, bytes.constData(), static_cast<size_t>(bytes.size()));
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

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray())
        return;
    const QJsonArray arr = doc.array();
    if (arr.isEmpty())
        return;

    const QString tag = arr.at(0).toString();
    if (tag == QLatin1String("result")) {
        emit result(arr.at(1).toInt(), arr.at(2).toVariant());
    } else if (tag == QLatin1String("error")) {
        emit error(arr.at(1).toInt(), arr.at(2).toString());
    } else if (tag == QLatin1String("event")) {
        emit event(arr.at(1).toString(), arr.at(2).toString(),
                   arr.at(3).toVariant());
    }
}
