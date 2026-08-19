#include "OmemoDevicesModel.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

OmemoDevicesModel::OmemoDevicesModel(QObject *parent)
    : QAbstractListModel(parent) {}

int OmemoDevicesModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant OmemoDevicesModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Device &d = m_rows.at(index.row());
    switch (role) {
    case DeviceRole:
        return d.device;
    case TrustRole:
        return d.trust;
    case ActiveRole:
        return d.active;
    case FingerprintRole:
        return d.fingerprint;
    case SettableRole:
        return d.settable();
    default:
        return {};
    }
}

QHash<int, QByteArray> OmemoDevicesModel::roleNames() const {
    return {
        {DeviceRole, "device"},
        {TrustRole, "trust"},
        {ActiveRole, "active"},
        {FingerprintRole, "fingerprint"},
        {SettableRole, "settable"},
    };
}

void OmemoDevicesModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &OmemoDevicesModel::refresh);
    emit backendChanged();
    refresh();
}

void OmemoDevicesModel::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    refresh();
}

void OmemoDevicesModel::setJid(const QString &jid) {
    if (m_jid == jid)
        return;
    m_jid = jid;
    emit jidChanged();
    refresh();
}

void OmemoDevicesModel::refresh() {
    // Whichever of these changed, the rows on screen belong to the old subject.
    m_lastRows.clear();
    setRows({});
    m_ownFingerprint.clear();
    m_ownDevice = 0;
    emit ownChanged();

    if (!m_backend || m_account.isEmpty() || m_jid.isEmpty())
        return;

    const QVariantMap acc{{QStringLiteral("acc"), m_account}};
    m_trustToken = m_backend->request(
        QStringLiteral("omemo"), QStringLiteral("trustList"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("jid"), m_jid}});
    m_blindToken = m_backend->request(QStringLiteral("omemo"),
                                      QStringLiteral("blindTrust"), acc);
    if (isOwn()) {
        m_fingerprintToken = m_backend->request(
            QStringLiteral("omemo"), QStringLiteral("own_fingerprint"), acc);
        m_deviceToken = m_backend->request(QStringLiteral("omemo"),
                                           QStringLiteral("device_id"), acc);
    }
}

int OmemoDevicesModel::settableCount() const {
    int n = 0;
    for (const Device &d : m_rows)
        if (d.settable())
            ++n;
    return n;
}

QString OmemoDevicesModel::commonTrust() const {
    QString shared;
    for (const Device &d : m_rows) {
        if (!d.settable())
            continue;
        if (shared.isEmpty())
            shared = d.trust;
        else if (shared != d.trust)
            return {};
    }
    return shared;
}

void OmemoDevicesModel::setTrust(int device, const QString &state) {
    if (!m_backend || m_account.isEmpty() || m_jid.isEmpty())
        return;
    // compromised is the backend's to set, and it rejects it from here anyway.
    if (state != QLatin1String("trusted") && state != QLatin1String("untrusted") &&
        state != QLatin1String("undecided"))
        return;
    m_backend->notify(QStringLiteral("omemo"), QStringLiteral("trust"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("jid"), m_jid},
                                  {QStringLiteral("device"), device},
                                  {QStringLiteral("state"), state}});
}

void OmemoDevicesModel::setAllTrust(const QString &state) {
    // One call per row: `omemo trust` is per device, and each emits its own
    // <TrustList>, so the rows converge as the replies land.
    for (const Device &d : m_rows)
        if (d.settable() && d.trust != state)
            setTrust(d.device, state);
}

void OmemoDevicesModel::setBlindTrust(bool on) {
    if (m_blindTrust == on)
        return;
    // Optimistic: the checkbox follows the click and <BlindTrust> confirms it.
    m_blindTrust = on;
    emit blindTrustChanged();
    if (!m_backend || m_account.isEmpty())
        return;
    m_backend->notify(QStringLiteral("omemo"), QStringLiteral("setBlindTrust"),
                      QVariantMap{{QStringLiteral("acc"), m_account},
                                  {QStringLiteral("value"), on ? 1 : 0}});
}

// The key list did not arrive. The rows on screen are the last ones that did,
// which is better than an empty list reading as "this peer has no keys".
void OmemoDevicesModel::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    for (int *t : {&m_trustToken, &m_blindToken, &m_fingerprintToken,
                   &m_deviceToken}) {
        if (*t == token) {
            *t = -1;
            return;
        }
    }
}

void OmemoDevicesModel::handleEvent(const QString &module, const QString &name,
                                    const QVariant &args) {
    const QVariantMap a = args.toMap();
    const QString acc = a.value(QStringLiteral("acc")).toString();
    if (acc != m_account || m_account.isEmpty())
        return;
    if (module == QLatin1String("omemo")) {
        if (name == QLatin1String("TrustList")) {
            if (a.value(QStringLiteral("jid")).toString() == m_jid)
                applyTrustList(a.value(QStringLiteral("trustList")).toList());
        } else if (name == QLatin1String("BlindTrust")) {
            applyBlindTrust(a.value(QStringLiteral("value")).toBool());
        }
    } else if (sessionUp(module, name, args)) {
        // The OMEMO store is built with the session, so anything asked for
        // before then came back empty. Ask again.
        refresh();
    }
}

void OmemoDevicesModel::handleResult(int token, const QVariant &data) {
    if (token == m_trustToken)
        applyTrustList(data.toList());
    else if (token == m_blindToken)
        applyBlindTrust(data.toBool());
    else if (token == m_fingerprintToken)
        applyOwnFingerprint(data.toString());
    else if (token == m_deviceToken)
        applyOwnDevice(data.toInt());
}

QList<OmemoDevicesModel::Device>
OmemoDevicesModel::visibleRows(const QVariantList &rows) const {
    QList<Device> out;
    out.reserve(rows.size());
    for (const QVariant &v : rows) {
        const QVariantMap m = v.toMap();
        Device d;
        d.device = m.value(QStringLiteral("device")).toInt();
        d.trust = m.value(QStringLiteral("trust")).toString();
        d.fingerprint = m.value(QStringLiteral("fingerprint")).toString();
        d.active = m.value(QStringLiteral("active")).toBool();
        if (isOwn() && m_ownDevice != 0 && d.device == m_ownDevice)
            continue;
        out.append(d);
    }
    return out;
}

// A trust flip keeps the same devices in the same order, so it updates in
// place; only a device coming or going resets the model.
void OmemoDevicesModel::setRows(const QList<Device> &next) {
    bool sameShape = next.size() == m_rows.size();
    for (int i = 0; sameShape && i < next.size(); ++i)
        sameShape = next.at(i).device == m_rows.at(i).device;

    if (!sameShape) {
        beginResetModel();
        m_rows = next;
        endResetModel();
        emit countChanged();
        emit summaryChanged();
        return;
    }

    bool changed = false;
    for (int i = 0; i < next.size(); ++i) {
        const Device &was = m_rows.at(i);
        const Device &now = next.at(i);
        QList<int> roles;
        if (was.trust != now.trust)
            roles << TrustRole << SettableRole;
        if (was.active != now.active)
            roles << ActiveRole;
        if (was.fingerprint != now.fingerprint)
            roles << FingerprintRole;
        if (roles.isEmpty())
            continue;
        m_rows[i] = now;
        changed = true;
        emit dataChanged(index(i), index(i), roles);
    }
    if (changed)
        emit summaryChanged();
}

void OmemoDevicesModel::applyTrustList(const QVariantList &rows) {
    m_lastRows = rows;
    setRows(visibleRows(rows));
}

void OmemoDevicesModel::applyOwnFingerprint(const QString &fingerprint) {
    if (m_ownFingerprint == fingerprint)
        return;
    m_ownFingerprint = fingerprint;
    emit ownChanged();
}

void OmemoDevicesModel::applyOwnDevice(int device) {
    if (m_ownDevice == device)
        return;
    m_ownDevice = device;
    emit ownChanged();
    // The list can arrive before we know which device we are, in which case it
    // still holds our own row; drop it now.
    setRows(visibleRows(m_lastRows));
}

void OmemoDevicesModel::applyBlindTrust(bool on) {
    if (m_blindTrust == on)
        return;
    m_blindTrust = on;
    emit blindTrustChanged();
}
