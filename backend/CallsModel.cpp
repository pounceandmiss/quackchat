#include "CallsModel.h"

#include "TackyBackend.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPermissions>

// Every event in and every transition out, so a window that moves on its own
// can be read back against what the backend actually said. The level is what
// keeps it quiet: without one, debug output is on.
//   QT_LOGGING_RULES='quack.*.debug=true' quackchat 2>calls.log
Q_LOGGING_CATEGORY(lcCalls, "quack.calls", QtWarningMsg)

namespace {
const QLatin1String kOutgoing("outgoing");
const QLatin1String kIncoming("incoming");
} // namespace

CallsModel::CallsModel(QObject *parent) : QAbstractListModel(parent) {}

int CallsModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_calls.size();
}

QVariant CallsModel::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_calls.size())
        return {};
    const Call &c = m_calls.at(index.row());
    switch (role) {
    case SidRole:       return c.sid;
    case AccountRole:   return c.account;
    case PeerRole:      return c.peer;
    case DirectionRole: return c.direction;
    case StateRole:     return c.state;
    case WarningRole:   return c.warning;
    case ReasonRole:    return c.reason;
    case TerminalRole:  return isTerminal(c.state);
    default:            return {};
    }
}

QHash<int, QByteArray> CallsModel::roleNames() const {
    return {{SidRole, "sid"},
            {AccountRole, "account"},
            {PeerRole, "peer"},
            {DirectionRole, "direction"},
            {StateRole, "state"},
            {WarningRole, "warning"},
            {ReasonRole, "reason"},
            {TerminalRole, "terminal"}};
}

bool CallsModel::isTerminal(const QString &state) {
    return state == QLatin1String("ended") || state == QLatin1String("failed");
}

void CallsModel::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend) {
        connect(m_backend, &TackyBackend::event, this, &CallsModel::handleEvent);
        connect(m_backend, &TackyBackend::result, this, &CallsModel::onResult);
        connect(m_backend, &TackyBackend::error, this, &CallsModel::onError);
    }
}

int CallsModel::indexOf(const QString &acc, const QString &sid) const {
    for (int i = 0; i < m_calls.size(); ++i)
        if (m_calls.at(i).sid == sid && m_calls.at(i).account == acc)
            return i;
    return -1;
}

void CallsModel::insertCall(const Call &call) {
    if (call.sid.isEmpty() || indexOf(call.account, call.sid) >= 0)
        return; // a carbon of our own propose, or a sid we already track
    const int pos = m_calls.size();
    beginInsertRows({}, pos, pos);
    m_calls.append(call);
    endInsertRows();
    emit countChanged();
    emit callAdded(call.account, call.sid);
}

void CallsModel::setState(const QString &acc, const QString &sid,
                          const QString &state) {
    const int i = indexOf(acc, sid);
    qCDebug(lcCalls).noquote()
        << "state" << acc << sid
        << (i < 0 ? QStringLiteral("(no row)") : m_calls.at(i).state) << "->"
        << state;
    if (i < 0 || m_calls.at(i).state == state)
        return;
    m_calls[i].state = state;
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {StateRole, TerminalRole});
}

void CallsModel::setField(const QString &acc, const QString &sid, Role role,
                          const QString &value) {
    const int i = indexOf(acc, sid);
    if (i < 0)
        return;
    if (role == WarningRole)
        m_calls[i].warning = value;
    else if (role == ReasonRole)
        m_calls[i].reason = value;
    else
        return;
    const QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {role});
}

void CallsModel::handleEvent(const QString &module, const QString &name,
                             const QVariant &args) {
    if (module != QLatin1String("calls"))
        return;
    const QVariantMap a = args.toMap();
    const QString sid = a.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty())
        return;
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    // Every calls event carries acc - client.tcl injects it - even though the
    // reference's signatures leave it out.
    const QString acc = a.value(QStringLiteral("acc")).toString();
    qCDebug(lcCalls).noquote() << "event" << name << acc << sid << a;

    if (name == QLatin1String("Outgoing")) {
        insertCall({sid, acc, a.value(QStringLiteral("to")).toString(), kOutgoing,
                    QStringLiteral("calling"), {}, {}});
    } else if (name == QLatin1String("Incoming")) {
        insertCall({sid, acc, a.value(QStringLiteral("from")).toString(), kIncoming,
                    QStringLiteral("incoming"), {}, {}});
    } else if (name == QLatin1String("Ringing")) {
        // Caller side only, and it can repeat once per answering device.
        const int i = indexOf(acc, sid);
        if (i >= 0 && m_calls.at(i).state == QLatin1String("calling"))
            setState(acc, sid, QStringLiteral("ringing"));
    } else if (name == QLatin1String("Active")) {
        // Clear unconditionally, not just on the first one: ICE losing consent
        // mid-call warns and leaves the call up, so the <Active> that follows
        // recovery is the only signal that the warning is stale.
        setField(acc, sid, WarningRole, {});
        setState(acc, sid, QStringLiteral("active"));
    } else if (name == QLatin1String("Ended")) {
        setState(acc, sid, QStringLiteral("ended"));
    } else if (name == QLatin1String("Failed")) {
        setField(acc, sid, ReasonRole,
                 a.value(QStringLiteral("reason")).toString());
        setState(acc, sid, QStringLiteral("failed"));
    } else if (name == QLatin1String("Warning")) {
        setField(acc, sid, WarningRole,
                 a.value(QStringLiteral("reason")).toString());
    }
}

// The sid in the reply is the one <Outgoing> already gave us, so there is
// nothing to do with it - this is only here to let the token go.
void CallsModel::onResult(int token, const QVariant &data) {
    Q_UNUSED(data)
    m_startTokens.remove(token);
}

void CallsModel::onError(int token, const QString &message) {
    const auto it = m_startTokens.constFind(token);
    if (it == m_startTokens.cend())
        return;
    const QPair<QString, QString> target = it.value();
    m_startTokens.erase(it);
    emit startFailed(target.first, target.second, message);
}

void CallsModel::withMicrophone(const QString &acc, const QString &peer,
                                const std::function<void()> &then) {
#ifdef Q_OS_ANDROID
    // Only Android gates the mic here. Desktop Linux has no permission backend
    // for it, and asking there would just answer Undetermined forever.
    const QMicrophonePermission perm;
    switch (qApp->checkPermission(perm)) {
    case Qt::PermissionStatus::Granted:
        then();
        return;
    case Qt::PermissionStatus::Denied:
        emit microphoneDenied(acc, peer);
        return;
    case Qt::PermissionStatus::Undetermined:
        // The prompt is modal to the user but async to us, so the call only
        // goes out once they have answered.
        qApp->requestPermission(perm, this,
                                [this, acc, peer, then](const QPermission &p) {
                                    if (p.status() == Qt::PermissionStatus::Granted)
                                        then();
                                    else
                                        emit microphoneDenied(acc, peer);
                                });
        return;
    }
#else
    Q_UNUSED(acc)
    Q_UNUSED(peer)
    then();
#endif
}

void CallsModel::start(const QString &acc, const QString &to) {
    if (!m_backend || acc.isEmpty() || to.isEmpty())
        return;
    withMicrophone(acc, to, [this, acc, to] {
        // A token only so a refusal ("not connected", no such account) has
        // somewhere to land - the sid comes from <Outgoing>, which the backend
        // emits before this ever replies.
        const int token = m_backend->request(
            QStringLiteral("calls"), QStringLiteral("start"),
            QVariantMap{{QStringLiteral("acc"), acc}, {QStringLiteral("to"), to}});
        m_startTokens.insert(token, {acc, to});
    });
}

void CallsModel::sendFor(const QString &acc, const QString &sid,
                         const QString &method, QVariantMap args) {
    if (!m_backend || indexOf(acc, sid) < 0)
        return;
    args.insert(QStringLiteral("acc"), acc);
    args.insert(QStringLiteral("sid"), sid);
    m_backend->notify(QStringLiteral("calls"), method, args);
}

void CallsModel::accept(const QString &acc, const QString &sid) {
    const int i = indexOf(acc, sid);
    if (i < 0 || m_calls.at(i).state != QLatin1String("incoming"))
        return;
    withMicrophone(acc, m_calls.at(i).peer, [this, acc, sid] {
        sendFor(acc, sid, QStringLiteral("accept"));
        // Nothing is emitted between our <proceed> and <Active>, which is a
        // long wait on a slow ICE path, so move the row ourselves.
        setState(acc, sid, QStringLiteral("connecting"));
    });
}

void CallsModel::reject(const QString &acc, const QString &sid,
                        const QString &reason) {
    QVariantMap args;
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendFor(acc, sid, QStringLiteral("reject"), args);
    // reject/hangup always emit <Ended> for a call the backend still knows
    // about. Setting it here too covers the race where the peer terminated
    // first and the backend has already forgotten this sid.
    setState(acc, sid, QStringLiteral("ended"));
}

void CallsModel::hangup(const QString &acc, const QString &sid,
                        const QString &reason) {
    QVariantMap args;
    if (!reason.isEmpty())
        args.insert(QStringLiteral("reason"), reason);
    sendFor(acc, sid, QStringLiteral("hangup"), args);
    setState(acc, sid, QStringLiteral("ended"));
}

void CallsModel::setDevices(const QString &acc, const QString &sid,
                            const QString &input, const QString &output) {
    sendFor(acc, sid, QStringLiteral("setDevices"),
            QVariantMap{{QStringLiteral("input"), input},
                        {QStringLiteral("output"), output}});
}

void CallsModel::dismiss(const QString &acc, const QString &sid) {
    const int i = indexOf(acc, sid);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_calls.removeAt(i);
    endRemoveRows();
    emit countChanged();
    emit callRemoved(acc, sid);
}
