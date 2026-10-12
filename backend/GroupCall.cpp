#include "GroupCall.h"

#include "MediaPermissions.h"
#include "TackyBackend.h"

#include <QLoggingCategory>
#include <QSet>

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
            // Not "state": Item has one, and a delegate could not take it.
            {StateRole, "legState"},
            {ReasonRole, "reason"},
            {WarningRole, "warning"},
            {VideoRole, "video"},
            {HasVideoRole, "hasVideo"},
            {RemoteVideoRole, "remoteVideo"}};
}

int GroupCallParticipants::indexOfKey(const QString &key) const {
    for (int i = 0; i < m_peers.size(); ++i)
        if (m_peers.at(i).key == key)
            return i;
    return -1;
}

void GroupCallParticipants::assign(const QList<Peer> &peers) {
    QSet<QString> keep;
    for (const Peer &p : peers)
        keep.insert(p.key);
    for (int i = m_peers.size() - 1; i >= 0; --i) {
        if (keep.contains(m_peers.at(i).key))
            continue;
        beginRemoveRows({}, i, i);
        m_peers.removeAt(i);
        endRemoveRows();
    }
    for (const Peer &p : peers) {
        const int i = indexOfKey(p.key);
        if (i < 0) {
            beginInsertRows({}, m_peers.size(), m_peers.size());
            m_peers.append(p);
            endInsertRows();
            continue;
        }
        Peer &old = m_peers[i];
        if (old.nick == p.nick && old.jid == p.jid && old.state == p.state
                && old.reason == p.reason && old.warning == p.warning
                && old.video == p.video && old.hasVideo == p.hasVideo
                && old.remoteVideo == p.remoteVideo)
            continue;
        old = p;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx);
    }
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

void GroupCallParticipants::endAll() {
    for (int i = 0; i < m_peers.size(); ++i) {
        Peer &p = m_peers[i];
        if (p.state != QLatin1String("failed"))
            p.state = QStringLiteral("ended");
        p.warning.clear();
        p.hasVideo = false;
        p.remoteVideo.clear();
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx);
    }
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

void GroupCall::setVideoFlags(bool video, bool cameraOn) {
    if (m_video != video) {
        m_video = video;
        emit videoChanged();
    }
    if (m_cameraOn != cameraOn) {
        m_cameraOn = cameraOn;
        emit cameraOnChanged();
    }
}

void GroupCall::setRoomState(bool active, int count, bool joined) {
    if (active == m_active && count == m_count && joined == m_joined)
        return;
    m_active = active;
    m_count = count;
    m_joined = joined;
    emit roomChanged();
}

void GroupCall::setPreview(const QVariantMap &preview) {
    if (m_preview == preview)
        return;
    m_preview = preview;
    emit previewChanged();
}

void GroupCall::tearDown() {
    setPreview({});
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

// A call held in the chat itself is joined there; otherwise `start` makes one
// in a room of its own - or joins the chat's last hosted call, if that is
// still going.
void GroupCall::join(bool video) {
    if (m_active)
        beginJoin(video, QStringLiteral("join"), {{QStringLiteral("jid"), m_jid}});
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
    m_leaving = false;
    m_participants.clear();
    setReason({});
    setWarning({});
    setPreview({});
    setVideoFlags(video, video);
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
                m_joinToken = request(method, a);
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
    // Ended here too, in case the backend already forgot the call; a call we
    // walked out of has no reason to show.
    m_leaving = true;
    setReason({});
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

void GroupCall::applyState(const QVariantMap &st) {
    const QString room = st.value(QStringLiteral("jid")).toString();
    const QString phase = st.value(QStringLiteral("phase")).toString();
    const int count = st.value(QStringLiteral("count")).toInt();
    qCDebug(lcGroupCall).noquote() << "state" << m_account << m_jid << room << phase
                                   << count;

    if (phase == kIdle) {
        // The banner's: someone else's call in the chat's own room.
        if (room == m_jid || room == m_callJid)
            setRoomState(count > 0, count, false);
        return;
    }
    if (phase == kEnded) {
        if (!m_callJid.isEmpty() && room != m_callJid)
            return;
        setRoomState(false, 0, false);
        m_leaving = false;
        if (!inCall())
            return; // leave() already ended it
        setReason(st.value(QStringLiteral("reason")).toString());
        tearDown();
        return;
    }
    if (m_leaving && room == m_callJid)
        return; // sent before the backend heard of our leave
    setCallJid(room);
    m_joinToken = -1;
    setReason({});
    setWarning(st.value(QStringLiteral("warning")).toString());
    setVideoFlags(flag(st.value(QStringLiteral("video"))),
                  flag(st.value(QStringLiteral("camera"))));
    setPreview(st.value(QStringLiteral("preview")).toMap());
    const qint64 startedAt = st.value(QStringLiteral("since")).toLongLong() / 1000;
    const bool clockMoved = startedAt != m_startedAt;
    m_startedAt = startedAt;

    QList<GroupCallParticipants::Peer> peers;
    for (const QVariant &v : st.value(QStringLiteral("participants")).toList()) {
        const QVariantMap p = v.toMap();
        GroupCallParticipants::Peer peer;
        peer.nick = p.value(QStringLiteral("nick")).toString();
        peer.jid = p.value(QStringLiteral("jid")).toString();
        peer.key = peer.jid.isEmpty() ? QLatin1Char('/') + peer.nick : peer.jid;
        peer.state = p.value(QStringLiteral("leg")).toString();
        peer.reason = p.value(QStringLiteral("reason")).toString();
        peer.warning = p.value(QStringLiteral("warning")).toString();
        peer.video = flag(p.value(QStringLiteral("video")));
        const QString stream = p.value(QStringLiteral("stream")).toString();
        peer.hasVideo = !stream.isEmpty();
        if (peer.hasVideo)
            peer.remoteVideo = {{QStringLiteral("name"), stream}};
        peers.append(peer);
    }
    m_participants.assign(peers);
    setRoomState(count > 0, count, phase == kLive);
    // Last: the rest is read on phaseChanged.
    if (m_phase != phase)
        setPhase(phase);
    else if (clockMoved)
        emit phaseChanged(); // startedAt's own signal
}

void GroupCall::startFailed(const QString &reason) {
    if (m_phase != kJoining)
        return;
    setReason(reason);
    tearDown();
}

void GroupCall::forget() {
    setRoomState(false, 0, false);
    if (!inCall())
        return;
    m_leaving = false;
    tearDown();
}

void GroupCall::handleResult(int token, const QVariant &data) {
    Q_UNUSED(data)
    if (token == m_joinToken)
        m_joinToken = -1;
}

void GroupCall::handleError(int token, const QString &message) {
    if (token != m_joinToken)
        return;
    m_joinToken = -1;
    qCDebug(lcGroupCall).noquote() << "join refused" << m_jid << message;
    if (m_phase == kJoining) {
        setReason(message);
        tearDown();
    }
}
