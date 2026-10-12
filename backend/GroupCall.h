// One chat's group call, from this account's side. Obtained from
// App.groupCalls.callFor() and kept for the life of the app, since a call
// outlives any one window on it.
//
// `jid` is the chat's; `callJid` the room the call is held in. For a call
// started here (and Dino's) that is a room of the call's own; for one held in
// the group chat itself (Movim's) it is the chat's own room. Requests go to
// the call's room.
//
// Everything here is copied from tacky's groupcall <State>, which
// GroupCallsModel routes in. `phase` follows the state's, except that an ended
// call stays `ended` until dismiss(), so the window can show how it ended.
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
        StateRole,       // expected | connecting | active | ended | failed
        ReasonRole,      // why a leg ended, when it ended on its own
        WarningRole,     // the leg's last <Warning>
        VideoRole,       // they announced a camera
        HasVideoRole,    // a <VideoTrack> stream is live
        RemoteVideoRole, // the <VideoTrack> arg map
    };
    Q_ENUM(Role)

    struct Peer {
        // The participant's bare JID, or "/nick" for one the room hides.
        QString key;
        QString nick;
        QString jid;
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
    // Rows whose key stays keep their place.
    void assign(const QList<Peer> &peers);
    void clear();
    // Every leg is over: nothing flows any more, and the rows say so.
    void endAll();

    // How many legs are up, for the tile grid to know if anyone is here yet.
    Q_INVOKABLE int activeCount() const;

signals:
    void countChanged();

private:
    QList<Peer> m_peers;
};

class GroupCall : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from App.groupCalls.callFor()")
    Q_PROPERTY(QString account READ account CONSTANT)
    Q_PROPERTY(QString jid READ jid CONSTANT)
    Q_PROPERTY(QString callJid READ callJid NOTIFY callJidChanged)
    // The room's call, whoever is in it: the banner's.
    Q_PROPERTY(bool active READ active NOTIFY roomChanged)
    Q_PROPERTY(int count READ count NOTIFY roomChanged)
    Q_PROPERTY(bool joined READ joined NOTIFY roomChanged)
    // Ours. See the top of the file.
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool inCall READ inCall NOTIFY phaseChanged)
    // Why the call ended, when it did not end by our hand.
    Q_PROPERTY(QString reason READ reason NOTIFY reasonChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY warningChanged)
    // Whether we offered a camera on join; the local toggle since.
    Q_PROPERTY(bool video READ video NOTIFY videoChanged)
    Q_PROPERTY(bool cameraOn READ cameraOn NOTIFY cameraOnChanged)
    Q_PROPERTY(bool sendingVideo READ sendingVideo NOTIFY previewChanged)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged)
    // When we went live, ms since the epoch; 0 before.
    Q_PROPERTY(qint64 startedAt READ startedAt NOTIFY phaseChanged)
    Q_PROPERTY(GroupCallParticipants *participants READ participants CONSTANT)

public:
    GroupCall(TackyBackend *backend, const QString &acc, const QString &jid,
              QObject *parent = nullptr);

    QString account() const { return m_account; }
    QString jid() const { return m_jid; }
    QString callJid() const { return m_callJid; }
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

    // Driven by GroupCallsModel, which owns the wire. Public so tests can feed
    // canned states without a backend.
    void applyState(const QVariantMap &state);
    void startFailed(const QString &reason);
    // A reattach's `get` left this call out: it is over.
    void forget();
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
    void setVideoFlags(bool video, bool cameraOn);
    void setPreview(const QVariantMap &preview);
    // Everything a call carries, gone: what a leave or a failed join leaves
    // behind is the reason and the rows, greyed.
    void tearDown();
    // The shared start of join() and answerInvite(): the state reset, the
    // permission prompts, then `method` with `args`.
    void beginJoin(bool video, const QString &method, QVariantMap args);
    void setRoomState(bool active, int count, bool joined);
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
    // We left and the ended state has not come back: states sent before the
    // backend heard of it must not put us back in.
    bool m_leaving = false;
    QString m_reason;
    QString m_warning;
    bool m_video = false;
    bool m_cameraOn = false;
    QVariantMap m_preview;
    qint64 m_startedAt = 0;
    GroupCallParticipants m_participants;
    int m_joinToken = -1;
};

#endif // GROUPCALL_H
