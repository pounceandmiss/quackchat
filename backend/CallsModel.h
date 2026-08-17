// Every live voice call, one row each, exposed to QML as `App.calls`.
//
// Rows are built from the event stream and rebuilt from `calls list`. The
// backend holds what is really in flight and this is a cache of it, which has to
// outlive any window: it hangs off AppController, not a shell.
//
// Rebuilding matters most on Android, where the interpreter lives in a service
// process that outlives this one and frames emitted while no UI is attached are
// dropped. Without it a UI returning from an activity destroy shows nothing for
// a call that is still up, and cannot even hang it up: every action below needs
// the row.
//
// Rows survive their call ending. A window needs to say "Ended" or offer a
// retry after "Failed", so a terminal row stays until QML calls dismiss().
//
// A row is identified by account AND sid. The sid names the session, not our
// end of it, so calling one of your own accounts from another puts two rows in
// here under one sid - and they move independently, right down to one ending
// while the other is still up.
//
// state, and who moves it:
//   calling     outgoing, propose sent                    <Outgoing>
//   ringing     outgoing, a peer device is alerting       <Ringing>
//   incoming    incoming, waiting for us to pick up       <Incoming>
//   connecting  we accepted, media is being set up        accept() (no event)
//   active      RTP flowing                               <Active>
//   ended       terminal, normal teardown                 <Ended>, hangup/reject,
//                                                         or a snapshot that no
//                                                         longer names the call
//   failed      terminal, unrecoverable                   <Failed>
// A call ends on exactly one of <Ended>/<Failed>; <Warning> is informational
// and only ever fills in the `warning` role.
#ifndef CALLSMODEL_H
#define CALLSMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <functional>

class TackyBackend;

class CallsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        SidRole = Qt::UserRole + 1,
        AccountRole,
        PeerRole,
        DirectionRole, // "outgoing" | "incoming"
        StateRole,
        WarningRole,  // last <Warning> reason; the call carried on
        ReasonRole,   // <Failed> reason
        TerminalRole, // state is ended or failed
    };
    Q_ENUM(Role)

    explicit CallsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBackend(TackyBackend *backend);

    // Ring `to` from `acc`. The row appears when <Outgoing> lands (the doc's
    // "reliable source no matter the transport"), not here; a backend refusal
    // arrives as startFailed().
    Q_INVOKABLE void start(const QString &acc, const QString &to);

    // acc as well as sid, for the reason at the top: a sid alone can name two
    // rows.
    Q_INVOKABLE void accept(const QString &acc, const QString &sid);
    Q_INVOKABLE void reject(const QString &acc, const QString &sid,
                            const QString &reason = {});
    Q_INVOKABLE void hangup(const QString &acc, const QString &sid,
                            const QString &reason = {});

    // Per-call mic/speaker override, leaving the persisted preference alone.
    // The UI drives devices through AudioDevices instead (as tacky's own GUI
    // does), so this is here for completeness.
    Q_INVOKABLE void setDevices(const QString &acc, const QString &sid,
                                const QString &input, const QString &output);

    // Drop a finished row. QML calls this when its window is done showing the
    // outcome; nothing else prunes, so a row lasts until then.
    Q_INVOKABLE void dismiss(const QString &acc, const QString &sid);

    // Ask what `acc` has in flight and reconcile the rows against the answer.
    // Driven by that account's conn events; exposed so a view can force one.
    Q_INVOKABLE void refreshFor(const QString &acc);

    static bool isTerminal(const QString &state);

    // Public so tests can drive the state machine with canned events, and hand
    // over a snapshot without guessing its token.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

signals:
    void countChanged();
    // A call appeared. The window manager spawns off the model itself, so this
    // is for anything that wants the bare fact (sounds, notifications).
    void callAdded(const QString &account, const QString &sid);
    void callRemoved(const QString &account, const QString &sid);
    // `calls start` was refused outright - no sid was ever assigned, so there
    // is no row to hang the message on.
    void startFailed(const QString &account, const QString &peer,
                     const QString &message);
    // Android turned us down at the permission prompt. Carries the call it was
    // asked for so the page that tried can be the one to say so.
    void microphoneDenied(const QString &account, const QString &peer);

private:
    struct Call {
        QString sid;
        QString account;
        QString peer;
        QString direction;
        QString state;
        QString warning;
        QString reason;
        // Insert order, so a reconcile can tell a row older than the request it
        // answers from one that arrived while that request was out.
        quint64 seq = 0;
    };

    // A `calls list` still out, and how far the rows had got when it went.
    struct PendingList {
        QString acc;
        quint64 seq;
    };

    static QString key(const QString &acc, const QString &sid);
    // tacky's state words to ours. Both spell one "ringing": tacky means we are
    // being rung, we mean a peer device is. Direction separates them.
    static QString snapshotState(const QString &raw, const QString &direction,
                                 bool peerRinging);
    // How far along a state is. A snapshot only moves a row forward, having been
    // taken before any local move since.
    static int progress(const QString &state);
    void applySnapshot(const QString &acc, const QVariantList &rows,
                       quint64 asOf);

    int indexOf(const QString &acc, const QString &sid) const;
    void insertCall(Call call);
    // Applies only if the row exists; emits dataChanged for the roles that moved.
    void setState(const QString &acc, const QString &sid, const QString &state);
    void setField(const QString &acc, const QString &sid, Role role,
                  const QString &value);
    // Fire-and-forget, or nothing if the row is gone.
    void sendFor(const QString &acc, const QString &sid, const QString &method,
                 QVariantMap args = {});
    // Runs `then` once the mic is ours to use. Straight through off Android.
    // acc/peer only name the call in a microphoneDenied().
    void withMicrophone(const QString &acc, const QString &peer,
                        const std::function<void()> &then);

    TackyBackend *m_backend = nullptr;
    QList<Call> m_calls;
    // token -> (acc, to), cleared by whichever of result/error comes back. The
    // backend answers every token it is given - including a failure thrown
    // before the method runs, and a server request that goes unanswered for a
    // minute of connected time - so entries do not pile up.
    QHash<int, QPair<QString, QString>> m_startTokens;
    QHash<int, PendingList> m_listTokens;
    // Torn down here since that account's last reconcile, so a snapshot taken
    // before the backend heard about it cannot put them back.
    QSet<QString> m_dismissed;
    quint64 m_seq = 0;
};

#endif // CALLSMODEL_H
