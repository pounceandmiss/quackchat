#include "StorageController.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

namespace {
const QLatin1String kStorage("storage");
} // namespace

StorageController::StorageController(QObject *parent) : QObject(parent) {}

// A pending migration gates as hard as `locked`: tacky installs no other module
// until it has run.
bool StorageController::gating(const QString &status) {
    return status == QLatin1String("locked") ||
           status == QLatin1String("pending-encrypt") ||
           status == QLatin1String("pending-decrypt");
}

// Startup only, because that is when a migration may run - tacky runs it before
// any account has connected. The same `pending-encrypt` reached from a running
// app is a request for the next launch, and honouring it there would migrate
// with every account live.
//
// Armed across locked -> pending-decrypt: unlocking is what reveals that
// request, and it still has to run before the app opens.
bool StorageController::gateActive() const {
    return m_gateArmed && gating(m_status);
}

void StorageController::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &StorageController::refresh);
}

void StorageController::refresh() {
    if (!m_backend)
        return;
    m_statusToken = m_backend->request(kStorage, QStringLiteral("status"));
}

// `storage` is the one module installed while locked, so unlike every other
// read in the app these want nothing beyond a live transport.
void StorageController::act(const QString &method, const QVariantMap &args) {
    if (!m_backend)
        return;
    setError(QString());
    m_done = 0;
    m_total = 0;
    emit progressChanged();
    setBusy(true);
    m_actionToken = m_backend->request(kStorage, method, args);
}

void StorageController::unlock(const QString &passphrase) {
    act(QStringLiteral("unlock"),
        {{QStringLiteral("passphrase"), passphrase}});
}

void StorageController::requestEncrypt() {
    act(QStringLiteral("requestEncrypt"));
}

void StorageController::requestDecrypt() {
    act(QStringLiteral("requestDecrypt"));
}

void StorageController::cancelPending() {
    act(QStringLiteral("cancelPending"));
}

void StorageController::encrypt(const QString &passphrase) {
    act(QStringLiteral("encrypt"),
        {{QStringLiteral("passphrase"), passphrase}});
}

void StorageController::decrypt() {
    act(QStringLiteral("decrypt"));
}

void StorageController::handleResult(int token, const QVariant &data) {
    // -1 is "no request of ours is out", not a token a backend ever hands out.
    if (token < 0)
        return;
    if (token == m_statusToken) {
        m_statusToken = -1;
        setStatus(data.toString());
    } else if (token == m_actionToken) {
        m_actionToken = -1;
        setBusy(false);
        // Ask rather than assume: unlocking an encrypted store that also has a
        // decrypt pending lands on pending-decrypt, not unlocked, and that
        // second gate has to appear.
        refresh();
    }
}

void StorageController::handleError(int token, const QString &message) {
    if (token < 0)
        return;
    if (token == m_statusToken) {
        m_statusToken = -1;
        // A status that never arrived leaves `status` empty and `ready` false,
        // so the app waits rather than guessing whether the store is encrypted.
        return;
    }
    if (token != m_actionToken)
        return;
    m_actionToken = -1;
    setBusy(false);
    setError(message);
}

void StorageController::handleEvent(const QString &module, const QString &name,
                                    const QVariant &args) {
    if (module != kStorage)
        return;
    if (name == QLatin1String("MigrateProgress")) {
        const QVariantMap a = args.toMap();
        m_done = a.value(QStringLiteral("done")).toInt();
        m_total = a.value(QStringLiteral("total")).toInt();
        emit progressChanged();
    } else if (name == QLatin1String("Unlocked") ||
               name == QLatin1String("MigrationComplete")) {
        // Also arrive when something else did the unlocking: on Android the
        // backend service outlives the UI, so another view of it can be what
        // answered the passphrase.
        refresh();
    }
}

void StorageController::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    // Disarmed by the first answer that is not gating, and never armed again:
    // from there on the app is open for business, and a migration requested
    // from inside it belongs to the next launch.
    if (m_gateArmed && !status.isEmpty() && !gating(status))
        m_gateArmed = false;
    emit statusChanged();
}

void StorageController::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void StorageController::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}
