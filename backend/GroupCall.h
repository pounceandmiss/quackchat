// One chat's group call, from this account's side: whether the chat has one,
// whether we are in it, and everyone in it with the state of the media session
// we hold to them. Obtained from App.groupCalls.callFor() and kept for the life of
// the app, since a call outlives any one window on it.
//
// `jid` is the chat's; `callJid` the room the call is held in. For a call
// started here (and Dino's) that is a room of the call's own; for one held in
// the group chat itself (Movim's) it is the chat's own room. Requests go to
// the call's room, and events find their way here by either.
//
// The room's state (active, count, joined) is read off its occupants: one is in
// the call while its `call` is non-empty, and counts once `announced`. Out of a
// call that room is the chat's own, so the banner shows an in-room call; a
// hosted one shows as its invite in the chat. In a call it is the call's room.
// `phase` is this side's: it says whether a window belongs on screen, which
// the backend has no opinion on.
//
//   idle     nothing to show; the banner in the chat says what the room has
//   joining  join() sent, waiting on <Joined> - the backend holds the room's
//            codec map still for a few seconds first
//   live     <Joined>, or a reattach found us in the call
//   ended    <Left>, leave(), or a join that failed - stays until dismiss(),
//            so the reason can be read and "Rejoin" is there to press
//
// The participants are the call room's occupants in the call, bar us. Each gets
// a leg: an ordinary `calls` session whose events carry the sid <Session>
// named for them. GroupCallsModel routes those here by sid, and the room's muc
// events here by room; CallsModel leaves legs alone.
#ifndef GROUPCALL_H
#define GROUPCALL_H

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class TackyBackend;

class GroupCallParticipants : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        NickRole = Qt::UserRole + 1,
        JidRole,         // bare, for the avatar; "" when the room hides it
        SidRole,
        StateRole,       // expected | connecting | active | ended | failed
        ReasonRole,      // why a leg ended, when it ended on its own
        WarningRole,     // the leg's last <Warning>
        VideoRole,       // they announced a camera
        HasVideoRole,    // a <VideoTrack> stream is live
        RemoteVideoRole, // the <VideoTrack> arg map
    };
    Q_ENUM(Role)

    struct Peer {
        // The occupant's real JID as the room gives it, or "/nick" for one
        // whose JID the room hides. Sessions name their peer by the former.
        QString key;
        QString nick;
        QString jid;
        QString sid;
        QString state;
        QString reason;
        QString warning;
        bool video = false;
        bool hasVideo = false;
        QVariantMap remoteVideo;
    };

    explicit GroupCallParticipants(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int indexOfKey(const QString &key) const;
    int indexOfSid(const QString &sid) const;
    QString keyAt(int row) const;
    // Insert or update by key. Fields left empty keep what the row has.
    void upsert(const Peer &peer);
    void remove(int row);
    void clear();
    void setState(int row, const QString &state, const QString &reason = {});
    void setWarning(int row, const QString &warning);
    void setRemoteVideo(int row, const QVariantMap &channel);
    // Every leg is over: nothing flows any more, and the rows say so.
    void endAll();

    // How many legs are up, for the tile grid to know if anyone is here yet.
    Q_INVOKABLE int activeCount() const;

signals:
    void countChanged();

private:
    void changed(int row, const QList<int> &roles);
    QList<Peer> m_peers;
};

class GroupCall : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from App.groupCalls.callFor()")
    Q_PROPERTY(QString account READ account CONSTANT)
    Q_PROPERTY(QString jid READ jid CONSTANT)
    Q_PROPERTY(QString callJid READ callJid NOTIFY callJidChanged)
    // The room's call, whoever is in it.
    Q_PROPERTY(bool active READ active NOTIFY roomChanged)
    Q_PROPERTY(int count READ count NOTIFY roomChanged)
    Q_PROPERTY(bool joined READ joined NOTIFY roomChanged)
    // Ours. See the top of the file.
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool inCall READ inCall NOTIFY phaseChanged)
    // Why the call ended, when it did not end by our hand.
    Q_PROPERTY(QString reason READ reason NOTIFY reasonChanged)
    // The room's last <Warning>: a participant the room hides, for one.
    Q_PROPERTY(QString warning READ warning NOTIFY warningChanged)
    // Whether we offered a camera on join; the local toggle since.
    Q_PROPERTY(bool video READ video NOTIFY videoChanged)
    Q_PROPERTY(bool cameraOn READ cameraOn NOTIFY cameraOnChanged)
    // Our own camera: the call's <VideoPreview>, which runs the whole call; on
    // a backend without one, a leg's, while that leg lasts.
    Q_PROPERTY(bool sendingVideo READ sendingVideo NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    // When we joined, ms since the epoch, for the clock in the window. 0 until
    // <Joined>.
    Q_PROPERTY(qint64 startedAt READ startedAt NOTIFY phaseChanged)
    Q_PROPERTY(GroupCallParticipants *participants READ participants CONSTANT)

public:
    GroupCall(TackyBackend *backend, const QString &acc, const QString &jid,
              QObject *parent = nullptr);

    QString account() const { return m_account; }
    QString jid() const { return m_jid; }
    QString callJid() const { return m_callJid; }
    // The room events name as this chat's call. GroupCallsModel sets it from
    // what the backend lists; tests set it directly.
    void setCallJid(const QString &room);
    bool active() const { return m_active; }
    int count() const { return m_count; }
    bool joined() const { return m_joined; }
    QString phase() const { return m_phase; }
    bool inCall() const;
    QString reason() const { return m_reason; }
    QString warning() const { return m_warning; }
    bool video() const { return m_video; }
    bool cameraOn() const { return m_cameraOn; }
    bool sendingVideo() const { return !m_preview.isEmpty(); }
    QVariantMap preview() const { return m_preview; }
    qint64 startedAt() const { return m_startedAt; }
    GroupCallParticipants *participants() { return &m_participants; }

    // Join the chat's call in progress, or start one in a room of its own. A
    // refusal lands in `reason` with the phase at ended.
    Q_INVOKABLE void join(bool video = false);
    // Answer the call invite stored at `ts` in `chat` (the message's own chat
    // JID, ?join and all), which names `room`.
    Q_INVOKABLE void answerInvite(const QString &chat, qlonglong ts,
                                  const QString &room, bool video = false);
    Q_INVOKABLE void declineInvite(const QString &chat, qlonglong ts);
    Q_INVOKABLE void leave();
    // Mute or unmute the camera on every leg at once.
    Q_INVOKABLE void setVideo(bool on);
    // XEP-0482: point someone at the room's call. A courtesy, not a ring.
    Q_INVOKABLE void invite(const QString &to);
    // The window is done showing how it ended.
    Q_INVOKABLE void dismiss();
    // Read the room's occupants again: the banner's state, and the
    // participants when we are in the call.
    Q_INVOKABLE void refresh();
    // The room that state is read from: the call's while in it, else the
    // chat's own.
    QString statusRoom() const;

    // Driven by GroupCallsModel, which owns the wire. Public so tests can feed
    // canned events without a backend.
    void handleEvent(const QString &name, const QVariantMap &args);
    // A `calls` event for a sid one of the legs owns. Answers whether it did.
    bool handleLegEvent(const QString &name, const QVariantMap &args);
    // A muc event for statusRoom(): its occupants may have changed.
    void handleRoomEvent(const QString &name, const QVariantMap &args);
    // `muc occupants` of statusRoom(), and our own nick there.
    void applyOccupants(const QVariantList &occupants, const QString &myNick);
    // `groupcall list` named this room: we are in its call, whatever the phase
    // thought - a reattach finds the call already up.
    // `preview` is the self-view's channel, empty when the backend has none;
    // `sessions` maps each peer's JID to our session's sid.
    void applyListed(bool video, const QVariantMap &preview = {},
                     const QVariantMap &sessions = {});
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void roomChanged();
    void callJidChanged();
    void phaseChanged();
    void reasonChanged();
    void warningChanged();
    void videoChanged();
    void cameraOnChanged();
    void previewChanged();

private:
    void setPhase(const QString &phase);
    void setReason(const QString &reason);
    void setWarning(const QString &warning);
    void clearVideo();
    void setPreview(const QVariantMap &preview);
    // Without the call's own self-view: a live leg's, if any is left.
    void pickLegPreview();
    // Everything a call carries, gone: what a leave or a failed join leaves
    // behind is the reason and the rows, greyed.
    void tearDown();
    // The shared start of join() and answerInvite(): the state reset, the
    // permission prompts, then `method` with `args`.
    void beginJoin(bool video, const QString &method, QVariantMap args);
    void setRoomState(bool active, int count, bool joined);
    // Ask again once the event loop comes round, so a burst of presences
    // costs one read.
    void scheduleRefresh();
    // A session with `peer`, replacing any earlier one.
    void bindSession(const QString &peer, const QString &sid);
    int request(const QString &method, QVariantMap args = {});
    void notify(const QString &method, QVariantMap args = {});

    TackyBackend *m_backend;
    QString m_account;
    QString m_jid;
    QString m_callJid;
    bool m_active = false;
    int m_count = 0;
    bool m_joined = false;
    QString m_phase = QStringLiteral("idle");
    QString m_reason;
    QString m_warning;
    bool m_video = false;
    bool m_cameraOn = false;
    QVariantMap m_preview;
    // The call's own self-view is up: legs' previews are not needed.
    bool m_callPreview = false;
    // Each leg's self-view by sid, the fallback while no call-wide one is.
    QHash<QString, QVariantMap> m_legPreviews;
    qint64 m_startedAt = 0;
    GroupCallParticipants m_participants;
    // Peer key -> our session's sid, kept apart from the rows so a session
    // that arrives before its occupant's presence still finds its row.
    QHash<QString, QString> m_sessions;
    // The last read of statusRoom(), applied once both halves are in.
    QString m_myNick;
    QVariantList m_occupants;
    bool m_haveNick = false;
    bool m_haveOccupants = false;
    bool m_refreshQueued = false;
    // The tokens of this room's own requests.
    enum class Pending { MyNick, Occupants, Join, CallsList };
    QHash<int, Pending> m_pending;
};

#endif // GROUPCALL_H
