#include "AccountsModel.h"

#include "BackendBinding.h"

#include "TackyBackend.h"

#include <utility> // std::as_const

AccountsModel::AccountsModel(QObject *parent) : QAbstractListModel(parent) {}

int AccountsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_accounts.size();
}

QVariant AccountsModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_accounts.size())
        return {};
    const Account &a = m_accounts.at(index.row());
    switch (role) {
    case JidRole:
        return a.jid;
    case ConnStateRole:
        return a.connState;
    case EnabledRole:
        return a.enabled;
    case StatusKnownRole:
        return a.statusKnown();
    default:
        return {};
    }
}

QHash<int, QByteArray> AccountsModel::roleNames() const {
    return {
        {JidRole, "jid"},
        {ConnStateRole, "connState"},
        {EnabledRole, "enabled"},
        {StatusKnownRole, "statusKnown"},
    };
}

void AccountsModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &AccountsModel::refresh);
    emit backendChanged();
    refresh();
}

void AccountsModel::refresh() {
    if (!m_backend)
        return;
    // Rows built while the enabled query is in flight have nothing to read the
    // flag off, so they count as unknown rather than as disabled.
    m_enabledKnown = false;
    // The list reply is bare JIDs, so enabled-ness needs its own query; either
    // can land first and applyList/applyEnabledList reconcile.
    //
    // `enabled` is an INTEGER(0/1) column and the filter is `WHERE enabled=$v`,
    // so this must be 1 and not JSON true - SQLite coerces that to 0 and hands
    // back the disabled accounts instead.
    m_listToken =
        m_backend->request(QStringLiteral("account"), QStringLiteral("list"),
                           QVariantMap{});
    m_enabledToken =
        m_backend->request(QStringLiteral("account"), QStringLiteral("list"),
                           QVariantMap{{QStringLiteral("enabled"), 1}});
}

// Re-fires conn <State> (and <ConnError> if one stands) with the value as it
// is now; tacky calls this the initial-state sync on attach.
void AccountsModel::pullConnState(const QString &jid) {
    if (!m_backend)
        return;
    for (const auto *event : {"State", "ConnError"})
        m_backend->notify(QStringLiteral("conn"), QStringLiteral("pull"),
                          QVariantMap{{QStringLiteral("acc"), jid},
                                      {QStringLiteral("event"),
                                       QLatin1String(event)}});
}

QString AccountsModel::connStateFor(const QString &jid) const {
    const int i = indexOfJid(jid);
    return i < 0 ? QString() : m_accounts.at(i).connState;
}

bool AccountsModel::isEnabled(const QString &jid) const {
    const int i = indexOfJid(jid);
    return i >= 0 && m_accounts.at(i).enabled;
}

// False for an account with no row at all: there is nothing to say about it.
bool AccountsModel::statusKnownFor(const QString &jid) const {
    const int i = indexOfJid(jid);
    return i >= 0 && m_accounts.at(i).statusKnown();
}

QString AccountsModel::firstJid() const {
    return m_accounts.isEmpty() ? QString() : m_accounts.first().jid;
}

void AccountsModel::add(const QString &jid, const QString &password) {
    if (!m_backend || jid.isEmpty())
        return;
    m_backend->notify(QStringLiteral("account"), QStringLiteral("add"),
                      QVariantMap{{QStringLiteral("acc"), jid},
                                  {QStringLiteral("password"), password}});
    m_backend->notify(QStringLiteral("account"), QStringLiteral("enable"),
                      QVariantMap{{QStringLiteral("acc"), jid}});
}

void AccountsModel::enable(const QString &jid) {
    if (m_backend)
        m_backend->notify(QStringLiteral("account"), QStringLiteral("enable"),
                          QVariantMap{{QStringLiteral("acc"), jid}});
}

void AccountsModel::disable(const QString &jid) {
    if (m_backend)
        m_backend->notify(QStringLiteral("account"), QStringLiteral("disable"),
                          QVariantMap{{QStringLiteral("acc"), jid}});
}

void AccountsModel::remove(const QString &jid) {
    if (m_backend)
        m_backend->notify(QStringLiteral("account"), QStringLiteral("remove"),
                          QVariantMap{{QStringLiteral("acc"), jid}});
}

void AccountsModel::handleEvent(const QString &module, const QString &name,
                                const QVariant &args) {
    const QVariantMap a = args.toMap();
    const QString acc = a.value(QStringLiteral("acc")).toString();
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    if (module == QLatin1String("account")) {
        if (name == QLatin1String("Added"))
            applyAdded(acc);
        else if (name == QLatin1String("Removed"))
            applyRemoved(acc);
        else if (name == QLatin1String("Enabled"))
            setEnabled(acc, true);
        else if (name == QLatin1String("Disabled"))
            setEnabled(acc, false);
    } else if (module == QLatin1String("conn")) {
        if (name == QLatin1String("State"))
            setConnState(acc, a.value(QStringLiteral("state")).toString());
        else if (name == QLatin1String("Ready"))
            setConnState(acc, QStringLiteral("connected"));
        else if (name == QLatin1String("AuthError"))
            setConnState(acc, QStringLiteral("auth-error"));
        else if (name == QLatin1String("ConnError"))
            setConnState(acc, QStringLiteral("conn-error"));
    }
}

void AccountsModel::handleResult(int token, const QVariant &data) {
    if (token == m_listToken)
        applyList(data.toList());
    else if (token == m_enabledToken)
        applyEnabledList(data.toList());
}

// The list did not arrive, so the rows stay as they are: one that has never been
// told still says so rather than guessing.
void AccountsModel::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token == m_listToken)
        m_listToken = -1;
    else if (token == m_enabledToken)
        m_enabledToken = -1;
}

int AccountsModel::indexOfJid(const QString &jid) const {
    for (int i = 0; i < m_accounts.size(); ++i)
        if (m_accounts.at(i).jid == jid)
            return i;
    return -1;
}

int AccountsModel::insertPos(const QString &jid) const {
    for (int i = 0; i < m_accounts.size(); ++i)
        if (jid.compare(m_accounts.at(i).jid, Qt::CaseInsensitive) < 0)
            return i;
    return m_accounts.size();
}

// Reconcile rows to exactly `jids`, keeping connState and enabled across it.
void AccountsModel::applyList(const QVariantList &jids) {
    QSet<QString> wanted;
    for (const QVariant &v : jids) {
        const QString jid = v.toString();
        if (!jid.isEmpty())
            wanted.insert(jid);
    }
    for (int i = m_accounts.size() - 1; i >= 0; --i) {
        if (!wanted.contains(m_accounts.at(i).jid)) {
            const QString gone = m_accounts.at(i).jid;
            beginRemoveRows({}, i, i);
            m_accounts.removeAt(i);
            endRemoveRows();
            emit removed(gone);
        }
    }
    for (const QString &jid : std::as_const(wanted)) {
        if (indexOfJid(jid) >= 0)
            continue;
        const int pos = insertPos(jid);
        beginInsertRows({}, pos, pos);
        Account a;
        a.jid = jid;
        a.enabled = m_enabledJids.contains(jid);
        a.enabledKnown = m_enabledKnown;
        m_accounts.insert(pos, a);
        endInsertRows();
    }
    // conn state only ever arrives as an event, so a row built from `account
    // list` has none: attaching to a backend whose accounts are already online
    // would otherwise sit on "connecting" until the next reconnect.
    for (const QString &jid : std::as_const(wanted))
        pullConnState(jid);
    emit countChanged();
}

void AccountsModel::applyEnabledList(const QVariantList &jids) {
    m_enabledJids.clear();
    for (const QVariant &v : jids) {
        const QString jid = v.toString();
        if (!jid.isEmpty())
            m_enabledJids.insert(jid);
    }
    // The reply covers every account, so absence from it settles a row as
    // disabled just as much as presence settles it as enabled.
    m_enabledKnown = true;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const bool en = m_enabledJids.contains(m_accounts.at(i).jid);
        if (m_accounts[i].enabled != en || !m_accounts[i].enabledKnown) {
            m_accounts[i].enabled = en;
            m_accounts[i].enabledKnown = true;
            ++m_connRev;
            emit connRevChanged();
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {EnabledRole, StatusKnownRole});
        }
    }
}

void AccountsModel::applyAdded(const QString &jid) {
    if (jid.isEmpty() || indexOfJid(jid) >= 0)
        return;
    const int pos = insertPos(jid);
    beginInsertRows({}, pos, pos);
    Account a;
    a.jid = jid;
    a.enabled = m_enabledJids.contains(jid);
    a.enabledKnown = m_enabledKnown;
    m_accounts.insert(pos, a);
    endInsertRows();
    emit countChanged();
}

void AccountsModel::applyRemoved(const QString &jid) {
    const int i = indexOfJid(jid);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_accounts.removeAt(i);
    endRemoveRows();
    m_enabledJids.remove(jid);
    emit countChanged();
    emit removed(jid);
}

void AccountsModel::setEnabled(const QString &jid, bool enabled) {
    if (enabled)
        m_enabledJids.insert(jid);
    else
        m_enabledJids.remove(jid);
    // <Enabled> can beat `account list` to the row; add it so the rail shows
    // the account the moment it comes online.
    int i = indexOfJid(jid);
    if (i < 0 && enabled) {
        applyAdded(jid);
        i = indexOfJid(jid);
    }
    if (i < 0 || (m_accounts.at(i).enabled == enabled &&
                  m_accounts.at(i).enabledKnown))
        return;
    m_accounts[i].enabled = enabled;
    m_accounts[i].enabledKnown = true;
    ++m_connRev;
    emit connRevChanged();
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {EnabledRole, StatusKnownRole});
}

void AccountsModel::setConnState(const QString &jid, const QString &state) {
    int i = indexOfJid(jid);
    // A conn event for an unlisted account (a fresh add) still says it exists.
    if (i < 0) {
        applyAdded(jid);
        i = indexOfJid(jid);
    }
    if (i < 0 || (m_accounts.at(i).connState == state &&
                  m_accounts.at(i).stateKnown))
        return;
    m_accounts[i].connState = state;
    m_accounts[i].stateKnown = true;
    ++m_connRev;
    emit connRevChanged();
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {ConnStateRole, StatusKnownRole});
}
