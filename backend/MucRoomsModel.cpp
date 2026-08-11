#include "MucRoomsModel.h"

MucRoomsModel::MucRoomsModel(QObject *parent) : QAbstractListModel(parent) {}

int MucRoomsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rooms.size();
}

// Role order: kKeys[i] is Qt::UserRole+1+i, and the QML role name is also the
// key tacky sends it under - keep it in step with the Role enum.
static const QList<QByteArray> kKeys = {"jid", "name", "occupants"};

QVariant MucRoomsModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_rooms.size())
        return {};
    const int i = role - (Qt::UserRole + 1);
    if (i < 0 || i >= kKeys.size())
        return {};
    return m_rooms.at(index.row()).value(QString::fromLatin1(kKeys.at(i)));
}

QHash<int, QByteArray> MucRoomsModel::roleNames() const {
    QHash<int, QByteArray> r;
    for (int i = 0; i < kKeys.size(); ++i)
        r.insert(Qt::UserRole + 1 + i, kKeys.at(i));
    return r;
}

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
    if (!m_rooms.isEmpty()) {
        beginResetModel();
        m_rooms.clear();
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
    m_rooms.clear();
    m_rooms.reserve(rooms.size());
    for (const QVariant &v : rooms)
        m_rooms.append(v.toMap());
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
