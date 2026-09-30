#include "GroupCall.h"

#include "MediaPermissions.h"
#include "TackyBackend.h"

#include <QDateTime>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcGroupCall, "quack.groupcall", QtWarningMsg)

namespace {
const QLatin1String kIdle("idle");
const QLatin1String kJoining("joining");
const QLatin1String kLive("live");
const QLatin1String kEnded("ended");

QString bareJid(const QString &jid) {
    const int slash = jid.indexOf(QLatin1Char('/'));
    return slash < 0 ? jid : jid.left(slash);
}

bool flag(const QVariant &v) {
    return v.toString() == QLatin1String("1") || v.toBool();
}
} // namespace

// ---------------------------------------------------------------------------

GroupCallParticipants::GroupCallParticipants(QObject *parent)
    : QAbstractListModel(parent) {}

int GroupCallParticipants::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_peers.size();
}

QVariant GroupCallParticipants::data(const QModelIndex &index, int role) const {
    if (index.row() < 0 || index.row() >= m_peers.size())
        return {};
    const Peer &p = m_peers.at(index.row());
    switch (role) {
    case NickRole:        return p.nick;
    case JidRole:         return p.jid;
    case SidRole:         return p.sid;
    case StateRole:       return p.state;
    case ReasonRole:      return p.reason;
    case WarningRole:     return p.warning;
    case VideoRole:       return p.video;
    case HasVideoRole:    return p.hasVideo;
    case RemoteVideoRole: return p.remoteVideo;
    default:              return {};
    }
}

QHash<int, QByteArray> GroupCallParticipants::roleNames() const {
    return {{NickRole, "nick"},
            {JidRole, "jid"},
            {SidRole, "sid"},
            // Not "state": Item has one, and a delegate could not take it.
            {StateRole, "legState"},
            {ReasonRole, "reason"},
            {WarningRole, "warning"},
            {VideoRole, "video"},
            {HasVideoRole, "hasVideo"},
            {RemoteVideoRole, "remoteVideo"}};
}

int GroupCallParticipants::indexOfNick(const QString &nick) const {
    for (int i = 0; i < m_peers.size(); ++i)
        if (m_peers.at(i).nick == nick)
            return i;
    return -1;
}

int GroupCallParticipants::indexOfSid(const QString &sid) const {
    if (sid.isEmpty())
        return -1;
    for (int i = 0; i < m_peers.size(); ++i)
        if (m_peers.at(i).sid == sid)
            return i;
    return -1;
}

void GroupCallParticipants::changed(int row, const QList<int> &roles) {
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

void GroupCallParticipants::upsert(const Peer &peer) {
    const int i = indexOfNick(peer.nick);
    if (i < 0) {
        beginInsertRows({}, m_peers.size(), m_peers.size());
        m_peers.append(peer);
        endInsertRows();
        emit countChanged();
        return;
    }
    Peer &p = m_peers[i];
    if (!peer.jid.isEmpty())
        p.jid = peer.jid;
    // A new leg to a participant we already show: the old stream is gone
    // with the old sid.
    if (!peer.sid.isEmpty() && peer.sid != p.sid) {
        p.sid = peer.sid;
        p.hasVideo = false;
        p.remoteVideo.clear();
    }
    if (!peer.state.isEmpty())
        p.state = peer.state;
    p.reason = peer.reason;
    p.warning.clear();
    p.video = peer.video;
    changed(i, {JidRole, SidRole, StateRole, ReasonRole, WarningRole, VideoRole,
                HasVideoRole, RemoteVideoRole});
}

void GroupCallParticipants::remove(int row) {
    if (row < 0 || row >= m_peers.size())
        return;
    beginRemoveRows({}, row, row);
    m_peers.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void GroupCallParticipants::clear() {
    if (m_peers.isEmpty())
        return;
    beginResetModel();
    m_peers.clear();
    endResetModel();
    emit countChanged();
}

void GroupCallParticipants::setState(int row, const QString &state,
                                     const QString &reason) {
    if (row < 0 || row >= m_peers.size())
        return;
    Peer &p = m_peers[row];
    p.state = state;
    p.reason = reason;
    QList<int> roles{StateRole, ReasonRole};
    if (state == QLatin1String("active") && !p.warning.isEmpty()) {
        p.warning.clear();
        roles << WarningRole;
    }
    if (state == QLatin1String("ended") || state == QLatin1String("failed")) {
        p.hasVideo = false;
        p.remoteVideo.clear();
        roles << HasVideoRole << RemoteVideoRole;
    }
    changed(row, roles);
}

void GroupCallParticipants::setWarning(int row, const QString &warning) {
    if (row < 0 || row >= m_peers.size())
        return;
    m_peers[row].warning = warning;
    changed(row, {WarningRole});
}

void GroupCallParticipants::setRemoteVideo(int row, const QVariantMap &channel) {
    if (row < 0 || row >= m_peers.size())
        return;
    m_peers[row].remoteVideo = channel;
    m_peers[row].hasVideo = !channel.isEmpty();
    changed(row, {HasVideoRole, RemoteVideoRole});
}

void GroupCallParticipants::endAll() {
    for (int i = 0; i < m_peers.size(); ++i)
        if (m_peers.at(i).state != QLatin1String("failed"))
            setState(i, QStringLiteral("ended"), m_peers.at(i).reason);
}

int GroupCallParticipants::activeCount() const {
    int n = 0;
    for (const Peer &p : m_peers)
        if (p.state == QLatin1String("active"))
            ++n;
    return n;
}

// ---------------------------------------------------------------------------

GroupCall::GroupCall(TackyBackend *backend, const QString &acc,
                     const QString &jid, QObject *parent)
    : QObject(parent), m_backend(backend), m_account(acc), m_jid(jid) {
    if (m_backend) {
        connect(m_backend, &TackyBackend::result, this, &GroupCall::handleResult);
        connect(m_backend, &TackyBackend::error, this, &GroupCall::handleError);
    }
}

bool GroupCall::inCall() const {
    return m_phase == kJoining || m_phase == kLive;
}

void GroupCall::setPhase(const QString &phase) {
    if (m_phase == phase)
        return;
    qCDebug(lcGroupCall).noquote() << "phase" << m_account << m_jid << m_phase
                                   << "->" << phase;
    m_phase = phase;
    emit phaseChanged();
}

void GroupCall::setReason(const QString &reason) {
    if (m_reason == reason)
        return;
    m_reason = reason;
    emit reasonChanged();
}

void GroupCall::setWarning(const QString &warning) {
    if (m_warning == warning)
        return;
    m_warning = warning;
    emit warningChanged();
}

void GroupCall::clearVideo() {
    m_callPreview = false;
    m_legPreviews.clear();
    setPreview({});
}

void GroupCall::setPreview(const QVariantMap &preview) {
    if (m_preview == preview)
        return;
    m_preview = preview;
    emit previewChanged();
}

void GroupCall::pickLegPreview() {
    if (m_callPreview)
        return;
    for (auto it = m_legPreviews.cbegin(); it != m_legPreviews.cend(); ++it) {
        if (it.value() == m_preview)
            return;
    }
    setPreview(m_legPreviews.isEmpty() ? QVariantMap{} : m_legPreviews.cbegin().value());
}

void GroupCall::tearDown() {
    clearVideo();
    m_participants.endAll();
    setPhase(kEnded);
}

void GroupCall::setCallJid(const QString &room) {
    if (room.isEmpty() || m_callJid == room)
        return;
    m_callJid = room;
    emit callJidChanged();
}

// Requests name the call's room, or the chat's own while no call is known -
// which is where an in-room call would be. One that names a stored invite
// (chat and timestamp) needs neither.
int GroupCall::request(const QString &method, QVariantMap args) {
    if (!m_backend)
        return -1;
    args.insert(QStringLiteral("acc"), m_account);
    if (!args.contains(QStringLiteral("timestamp")) && !args.contains(QStringLiteral("jid")))
        args.insert(QStringLiteral("jid"), m_callJid.isEmpty() ? m_jid : m_callJid);
    return m_backend->request(QStringLiteral("groupcall"), method, args);
}

void GroupCall::notify(const QString &method, QVariantMap args) {
    if (!m_backend)
        return;
    args.insert(QStringLiteral("acc"), m_account);
    if (!args.contains(QStringLiteral("timestamp")) && !args.contains(QStringLiteral("jid")))
        args.insert(QStringLiteral("jid"), m_callJid.isEmpty() ? m_jid : m_callJid);
    m_backend->notify(QStringLiteral("groupcall"), method, args);
}

// A call in progress is joined; with none, one is started in a room of its
// own, which <Started> names.
void GroupCall::join(bool video) {
    if (m_active && !m_callJid.isEmpty())
        beginJoin(video, QStringLiteral("join"), {});
    else
        beginJoin(video, QStringLiteral("start"), {{QStringLiteral("chat"), m_jid}});
}

void GroupCall::answerInvite(const QString &chat, qlonglong ts,
                             const QString &room, bool video) {
    if (inCall())
        return;
    setCallJid(room);
    beginJoin(video, QStringLiteral("join"),
              {{QStringLiteral("chat"), chat}, {QStringLiteral("timestamp"), ts}});
}

void GroupCall::declineInvite(const QString &chat, qlonglong ts) {
    notify(QStringLiteral("decline"),
           {{QStringLiteral("chat"), chat}, {QStringLiteral("timestamp"), ts}});
}

void GroupCall::beginJoin(bool video, const QString &method, QVariantMap args) {
    if (inCall())
        return;
    m_participants.clear();
    setReason({});
    setWarning({});
    clearVideo();
    if (m_video != video) {
        m_video = video;
        emit videoChanged();
    }
    if (m_cameraOn != video) {
        m_cameraOn = video;
        emit cameraOnChanged();
    }
    m_startedAt = 0;
    setPhase(kJoining);
    // The prompts are async on Android, so the phase moves first and the
    // window is up while they are on screen.
    media::withMicrophone(
        this,
        [this, video, method, args] {
            media::withCamera(this, video, [this, video, method, args] {
                if (m_phase != kJoining)
                    return; // left, or dismissed, while the prompt was up
                QVariantMap a = args;
                a.insert(QStringLiteral("video"), video ? 1 : 0);
                const int token = request(method, a);
                if (token >= 0)
                    m_pending.insert(token, Pending::Join);
            });
        },
        [this] {
            if (m_phase != kJoining)
                return;
            setReason(tr("Microphone access is off for Quack."));
            tearDown();
        });
}

void GroupCall::leave() {
    if (!inCall())
        return;
    notify(QStringLiteral("leave"));
    // <Left> follows for a call the backend still holds; ending here too
    // covers a backend that already forgot it, and keeps the reason ours: a
    // call we walked out of has nothing to explain.
    tearDown();
}

void GroupCall::setVideo(bool on) {
    if (m_cameraOn != on) {
        m_cameraOn = on;
        emit cameraOnChanged();
    }
    if (inCall())
        notify(QStringLiteral("setVideo"), {{QStringLiteral("on"), on ? 1 : 0}});
}

void GroupCall::invite(const QString &to) {
    const QString bare = bareJid(to.trimmed());
    if (bare.isEmpty())
        return;
    notify(QStringLiteral("invite"), {{QStringLiteral("to"), bare}});
}

void GroupCall::dismiss() {
    if (inCall())
        return;
    m_participants.clear();
    setReason({});
    setWarning({});
    m_startedAt = 0;
    setPhase(kIdle);
}

void GroupCall::refresh() {
    const int token = request(QStringLiteral("status"));
    if (token >= 0)
        m_pending.insert(token, Pending::Status);
    if (inCall()) {
        const int t = request(QStringLiteral("participants"));
        if (t >= 0)
            m_pending.insert(t, Pending::Participants);
    }
}

void GroupCall::handleEvent(const QString &name, const QVariantMap &a) {
    qCDebug(lcGroupCall).noquote() << "event" << name << m_account << m_jid << a;
    if (name == QLatin1String("Changed")) {
        // A chat hears of more than one room: its own, where an in-room call
        // would be, and the room of a call started from it. In a call, only
        // that call's news counts; out of one, a room with a call on becomes
        // the chat's call.
        const QString room = a.value(QStringLiteral("jid")).toString();
        if (!room.isEmpty() && room != m_callJid) {
            if (inCall() && !m_callJid.isEmpty())
                return;
            if (!flag(a.value(QStringLiteral("active"))) && !m_callJid.isEmpty())
                return;
            setCallJid(room);
        }
        applyStatus(a);
    } else if (name == QLatin1String("Started")) {
        setCallJid(a.value(QStringLiteral("jid")).toString());
    } else if (name == QLatin1String("StartFailed")) {
        if (m_phase == kJoining) {
            setReason(a.value(QStringLiteral("reason")).toString());
            tearDown();
        }
    } else if (name == QLatin1String("Joined")) {
        setCallJid(a.value(QStringLiteral("jid")).toString());
        m_startedAt = QDateTime::currentMSecsSinceEpoch();
        setPhase(kLive);
    } else if (name == QLatin1String("PeerJoined")) {
        GroupCallParticipants::Peer p;
        p.nick = a.value(QStringLiteral("nick")).toString();
        p.jid = bareJid(a.value(QStringLiteral("peer")).toString());
        p.sid = a.value(QStringLiteral("sid")).toString();
        p.state = QStringLiteral("connecting");
        p.video = flag(a.value(QStringLiteral("video")));
        m_participants.upsert(p);
    } else if (name == QLatin1String("PeerLeft")) {
        const int i = m_participants.indexOfNick(
            a.value(QStringLiteral("nick")).toString());
        const QString reason = a.value(QStringLiteral("reason")).toString();
        if (reason == QLatin1String("left the call"))
            m_participants.remove(i);
        else if (i >= 0 && m_participants.data(m_participants.index(i),
                                               GroupCallParticipants::StateRole)
                                   .toString() != QLatin1String("failed"))
            m_participants.setState(i, QStringLiteral("ended"), reason);
    } else if (name == QLatin1String("Left")) {
        if (inCall())
            setReason(a.value(QStringLiteral("reason")).toString());
        if (m_phase != kIdle)
            tearDown();
    } else if (name == QLatin1String("Warning")) {
        setWarning(a.value(QStringLiteral("reason")).toString());
    } else if (name == QLatin1String("VideoPreview")) {
        if (a.value(QStringLiteral("name")).toString().isEmpty()
                && a.value(QStringLiteral("id")).toString().isEmpty())
            return;
        m_callPreview = true;
        setPreview(a);
    }
}

bool GroupCall::handleLegEvent(const QString &name, const QVariantMap &a) {
    const QString sid = a.value(QStringLiteral("sid")).toString();
    const int i = m_participants.indexOfSid(sid);
    if (i < 0)
        return false;
    if (name == QLatin1String("Active")) {
        m_participants.setState(i, QStringLiteral("active"));
    } else if (name == QLatin1String("Ended")) {
        m_participants.setState(i, QStringLiteral("ended"));
        m_legPreviews.remove(sid);
        pickLegPreview();
    } else if (name == QLatin1String("Failed")) {
        m_participants.setState(i, QStringLiteral("failed"),
                                a.value(QStringLiteral("reason")).toString());
        m_legPreviews.remove(sid);
        pickLegPreview();
    } else if (name == QLatin1String("Warning")) {
        m_participants.setWarning(i, a.value(QStringLiteral("reason")).toString());
    } else if (name == QLatin1String("VideoTrack")) {
        if (a.value(QStringLiteral("direction")).toString() == QLatin1String("incoming"))
            m_participants.setRemoteVideo(i, a);
    } else if (name == QLatin1String("VideoEnded")) {
        m_participants.setRemoteVideo(i, {});
    } else if (name == QLatin1String("VideoPreview")) {
        if (!a.value(QStringLiteral("name")).toString().isEmpty()) {
            m_legPreviews.insert(sid, a);
            pickLegPreview();
        }
    }
    return true;
}

void GroupCall::applyStatus(const QVariantMap &s) {
    const bool active = flag(s.value(QStringLiteral("active")));
    const int count = s.value(QStringLiteral("count")).toInt();
    const bool joined = flag(s.value(QStringLiteral("joined")));
    if (active == m_active && count == m_count && joined == m_joined)
        return;
    m_active = active;
    m_count = count;
    m_joined = joined;
    emit roomChanged();
}

void GroupCall::applyParticipants(const QVariantList &rows) {
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        const QString state = r.value(QStringLiteral("state")).toString();
        // `none` is the answer for everyone while we are not in the call, and
        // for our own entry when we are.
        if (state == QLatin1String("none"))
            continue;
        GroupCallParticipants::Peer p;
        p.nick = r.value(QStringLiteral("nick")).toString();
        p.jid = bareJid(r.value(QStringLiteral("jid")).toString());
        p.sid = r.value(QStringLiteral("sid")).toString();
        p.video = flag(r.value(QStringLiteral("video")));
        if (state == QLatin1String("expected") || state == QLatin1String("active") ||
            state == QLatin1String("ended") || state == QLatin1String("failed"))
            p.state = state;
        else
            p.state = QStringLiteral("connecting"); // proceeded, new, connecting
        m_participants.upsert(p);
    }
}

void GroupCall::applyListed(bool video, const QVariantMap &preview) {
    if (!preview.isEmpty()) {
        m_callPreview = true;
        setPreview(preview);
    }
    if (m_video != video) {
        m_video = video;
        emit videoChanged();
    }
    if (m_cameraOn != video) {
        m_cameraOn = video;
        emit cameraOnChanged();
    }
    if (m_phase == kLive)
        return;
    // How long the call has been up is lost with the process; the clock
    // starts from here.
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    setReason({});
    setPhase(kLive);
    const int t = request(QStringLiteral("participants"));
    if (t >= 0)
        m_pending.insert(t, Pending::Participants);
}

void GroupCall::handleResult(int token, const QVariant &data) {
    const auto it = m_pending.constFind(token);
    if (it == m_pending.cend())
        return;
    const Pending what = it.value();
    m_pending.erase(it);
    switch (what) {
    case Pending::Status:
        applyStatus(data.toMap());
        break;
    case Pending::Participants:
        applyParticipants(data.toList());
        break;
    case Pending::Join:
        // <Joined> is what moves the phase; the reply only says the request
        // was taken.
        break;
    }
}

void GroupCall::handleError(int token, const QString &message) {
    const auto it = m_pending.constFind(token);
    if (it == m_pending.cend())
        return;
    const Pending what = it.value();
    m_pending.erase(it);
    qCDebug(lcGroupCall).noquote() << "request failed" << m_jid << message;
    if (what == Pending::Join && m_phase == kJoining) {
        setReason(message);
        tearDown();
    }
}
