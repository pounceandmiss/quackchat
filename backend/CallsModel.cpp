#include "CallsModel.h"

#include "BackendBinding.h"
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
    // No re-seed on the connected edge: it names no account and `calls list`
    // takes one. The conn events handleEvent watches are that same edge, split
    // up by the account it happened to.
    if (m_backend)
        bindBackendWithoutReseed(this, m_backend);
}

QString CallsModel::key(const QString &acc, const QString &sid) {
    return acc + QLatin1Char('\n') + sid;
}

int CallsModel::indexOf(const QString &acc, const QString &sid) const {
    for (int i = 0; i < m_calls.size(); ++i)
        if (m_calls.at(i).sid == sid && m_calls.at(i).account == acc)
            return i;
    return -1;
}

void CallsModel::insertCall(Call call) {
    if (call.sid.isEmpty() || indexOf(call.account, call.sid) >= 0)
        return; // a carbon of our own propose, or a sid we already track
    call.seq = ++m_seq;
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
    const QVariantMap a = args.toMap();
    // Event names arrive bare on the JSON wire (the backend strips the Tcl <>).
    // Every calls event carries acc - client.tcl injects it - even though the
    // reference's signatures leave it out.
    const QString acc = a.value(QStringLiteral("acc")).toString();

    // Another module's events, but our trigger. <State> rather than <Ready>,
    // which fires from the same three lines of the backend and so would only
    // ask twice: this one is also the only half that can be pulled, and a
    // pull is what a UI reattaching to a backend that never went away gets.
    if (module == QLatin1String("conn")) {
        if (name == QLatin1String("State") &&
            a.value(QStringLiteral("state")).toString() ==
                QLatin1String("connected"))
            refreshFor(acc);
        return;
    }
    if (module != QLatin1String("calls"))
        return;
    const QString sid = a.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty())
        return;
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

void CallsModel::refreshFor(const QString &acc) {
    if (!m_backend || acc.isEmpty())
        return;
    // Only the newest list for an account counts: an older one describes a
    // moment already passed, and would undo what the newer one settled.
    for (auto it = m_listTokens.begin(); it != m_listTokens.end();)
        it = it->acc == acc ? m_listTokens.erase(it) : std::next(it);
    const int token = m_backend->request(
        QStringLiteral("calls"), QStringLiteral("list"),
        QVariantMap{{QStringLiteral("acc"), acc}});
    m_listTokens.insert(token, {acc, m_seq});
}

void CallsModel::handleResult(int token, const QVariant &data) {
    const auto pending = m_listTokens.constFind(token);
    if (pending != m_listTokens.cend()) {
        const PendingList p = pending.value();
        m_listTokens.erase(pending);
        applySnapshot(p.acc, data.toList(), p.seq);
        return;
    }
    // The sid in a start reply is the one <Outgoing> already gave us, so there
    // is nothing to do with it - this is only here to let the token go.
    m_startTokens.remove(token);
}

void CallsModel::handleError(int token, const QString &message) {
    if (m_listTokens.remove(token)) {
        // A backend too old for the method, or an account that went away
        // between the conn event and the request. Nothing to show a user, and
        // the rows we hold are still the best picture there is.
        qCDebug(lcCalls).noquote() << "list failed" << message;
        return;
    }
    const auto it = m_startTokens.constFind(token);
    if (it == m_startTokens.cend())
        return;
    const QPair<QString, QString> target = it.value();
    m_startTokens.erase(it);
    emit startFailed(target.first, target.second, message);
}

QString CallsModel::snapshotState(const QString &raw, const QString &direction,
                                  bool peerRinging) {
    if (raw == QLatin1String("ringing") && direction == kIncoming)
        return QStringLiteral("incoming");
    if (raw == QLatin1String("proposed") && direction == kOutgoing)
        return peerRinging ? QStringLiteral("ringing")
                           : QStringLiteral("calling");
    if (raw == QLatin1String("active"))
        return QStringLiteral("active");
    // Cleanup drops a session in the same frame that ends it, so this cannot
    // arrive - but do not invent a live call if it ever does.
    if (isTerminal(raw))
        return raw;
    // proceeded, new, connecting, and anything a later backend adds: media
    // coming up is the safe reading of all of them.
    return QStringLiteral("connecting");
}

int CallsModel::progress(const QString &state) {
    if (state == QLatin1String("incoming") || state == QLatin1String("calling"))
        return 0;
    if (state == QLatin1String("ringing"))
        return 1;
    if (state == QLatin1String("connecting"))
        return 2;
    if (state == QLatin1String("active"))
        return 3;
    return 4; // ended, failed
}

void CallsModel::applySnapshot(const QString &acc, const QVariantList &rows,
                               quint64 asOf) {
    QSet<QString> seen;
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        const QString sid = r.value(QStringLiteral("sid")).toString();
        if (sid.isEmpty())
            continue;
        seen.insert(sid);
        if (m_dismissed.contains(key(acc, sid)))
            continue;
        const QString direction = r.value(QStringLiteral("direction")).toString();
        const QString state =
            snapshotState(r.value(QStringLiteral("state")).toString(), direction,
                          r.value(QStringLiteral("peer_ringing")).toBool());

        const int i = indexOf(acc, sid);
        if (i < 0) {
            insertCall({sid, acc, r.value(QStringLiteral("peer")).toString(),
                        direction, state, {}, {}, 0});
            continue;
        }
        // Forward only. A local move made since the request went out - an
        // accept() that set connecting, a hangup() that set ended - is newer
        // than this. It also holds an active row through the connecting that
        // follows an ICE consent loss, as the event path does.
        const Call &c = m_calls.at(i);
        if (c.direction != direction || isTerminal(c.state) ||
            progress(state) <= progress(c.state))
            continue;
        setState(acc, sid, state);
    }

    // Whatever the backend does not mention is over. Rows younger than the
    // request are exempt: the snapshot predates them and says nothing of them.
    QStringList gone;
    for (const Call &c : std::as_const(m_calls))
        if (c.account == acc && c.seq <= asOf && !seen.contains(c.sid) &&
            !isTerminal(c.state))
            gone.append(c.sid);
    // Collected first: setState runs QML bindings, one of which can reach
    // dismiss() and remove from the list being walked.
    for (const QString &sid : std::as_const(gone))
        setState(acc, sid, QStringLiteral("ended"));

    // Only lists already in flight need shadowing; by the next reconcile the
    // backend has heard about the teardown.
    for (auto it = m_dismissed.begin(); it != m_dismissed.end();)
        it = it->startsWith(acc + QLatin1Char('\n')) ? m_dismissed.erase(it)
                                                     : std::next(it);
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
    m_dismissed.insert(key(acc, sid));
    beginRemoveRows({}, i, i);
    m_calls.removeAt(i);
    endRemoveRows();
    emit countChanged();
    emit callRemoved(acc, sid);
}
