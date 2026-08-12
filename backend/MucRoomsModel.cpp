#include "MucRoomsModel.h"

// In Role order, which is what lines the keys up with the roles.
MucRoomsModel::MucRoomsModel(QObject *parent)
    : MapListModel({"jid", "name", "occupants"}, parent) {}

void MucRoomsModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::result, this, &MucRoomsModel::handleResult);
        connect(m_backend, &TackyBackend::error, this, &MucRoomsModel::handleError);
    }
    emit backendChanged();
}

void MucRoomsModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    // Another account's server hosts other rooms, and its nick is its own.
    clear();
    setDefaultNick({});
}

void MucRoomsModel::discover(const QString &service) {
    if (!m_backend || m_account.isEmpty() || service.isEmpty())
        return;
    // Whatever is in flight is abandoned rather than cancelled: its reply will
    // arrive under a token nothing matches any more, and be dropped.
    clear();
    m_token = m_backend->request(QStringLiteral("muc"),
                                 QStringLiteral("discoverRooms"),
                                 QVariantMap{{QStringLiteral("acc"), m_account},
                                             {QStringLiteral("jid"), service}});
    emit loadingChanged();
}

void MucRoomsModel::clear() {
    const bool wasLoading = m_token != 0;
    m_token = 0;
    setError({});
    setLoaded(false);
    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
    }
    if (wasLoading)
        emit loadingChanged();
}

void MucRoomsModel::requestDefaultNick() {
    if (!m_backend || m_account.isEmpty())
        return;
    m_nickToken = m_backend->request(QStringLiteral("bookmarks"),
                                     QStringLiteral("defaultNick"),
                                     QVariantMap{{QStringLiteral("acc"), m_account}});
}

void MucRoomsModel::handleResult(int token, const QVariant &data) {
    if (m_nickToken != 0 && token == m_nickToken) {
        m_nickToken = 0;
        setDefaultNick(data.toString());
        return;
    }
    if (m_token == 0 || token != m_token)
        return;
    m_token = 0;
    applyRooms(data.toList());
    setLoaded(true);
    emit loadingChanged();
}

void MucRoomsModel::handleError(int token, const QString &message) {
    if (m_nickToken != 0 && token == m_nickToken) {
        // No nick is a gap the dialog fills from the account JID, not a failure
        // worth showing anyone.
        m_nickToken = 0;
        return;
    }
    if (m_token == 0 || token != m_token)
        return;
    m_token = 0;
    setError(message);
    emit loadingChanged();
}

void MucRoomsModel::applyRooms(const QVariantList &rooms) {
    beginResetModel();
    m_items.clear();
    m_items.reserve(rooms.size());
    for (const QVariant &v : rooms)
        m_items.append(v.toMap());
    endResetModel();
}

void MucRoomsModel::setError(const QString &message) {
    if (m_error == message)
        return;
    m_error = message;
    emit errorChanged();
}

void MucRoomsModel::setDefaultNick(const QString &nick) {
    if (m_defaultNick == nick)
        return;
    m_defaultNick = nick;
    emit defaultNickChanged();
}

void MucRoomsModel::setLoaded(bool v) {
    if (m_loaded == v)
        return;
    m_loaded = v;
    emit loadedChanged();
}
