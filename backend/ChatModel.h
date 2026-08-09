// The sliding message window for one chat, ported from tacky's gui/chat.tcl.
// The model owns the invariants (insertion, at-tail gate, per-direction request
// tags, <Confirmed> repositioning); QML keeps only viewport and rendering.
//
// Newest-first (row 0 = newest) to match the feed's BottomToTop ListView.
// `timestamp` is the message id - unique, the backend bumps collisions.
#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "TackyBackend.h"

class ChatModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(TackyBackend *backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString account READ account WRITE setAccount NOTIFY accountChanged)
    Q_PROPERTY(QString chat READ chat WRITE setChat NOTIFY chatChanged)
    Q_PROPERTY(bool groupchat READ groupchat WRITE setGroupchat NOTIFY groupchatChanged)
    // The window contains the conversation tail (gates live inserts).
    Q_PROPERTY(bool atTail READ atTail NOTIFY atTailChanged)
    // Inside this chat's catchup bracket; gates live inserts too.
    Q_PROPERTY(bool catchupBusy READ catchupBusy NOTIFY catchupBusyChanged)
    // A page of older history is out, possibly for good: a `before` fill that
    // comes up short locally reaches for MAM, and that leg has no timeout. The
    // view shows this rather than looking idle.
    Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY loadingOlderChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        BodyRole,        // text body or media caption, from the content union
        OutgoingRole,
        ServerStatusRole,
        FromRole,
        RetractedRole,   // tombstone: render the deleted-message placeholder
        RawRole,
    };
    Q_ENUM(Role)

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    TackyBackend *backend() const { return m_backend; }
    QString account() const { return m_account; }
    QString chat() const { return m_chat; }
    bool groupchat() const { return m_groupchat; }
    bool atTail() const { return m_atTail; }
    bool catchupBusy() const { return m_catchupBusy; }
    bool loadingOlder() const {
        return m_inflight.contains(QStringLiteral("old")) ||
               m_inflight.contains(QStringLiteral("init"));
    }
    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setChat(const QString &chat);
    void setGroupchat(bool v);

    Q_INVOKABLE void loadInitial();               // newest page (no cursor)
    Q_INVOKABLE void loadOlder();                 // page below the oldest row
    Q_INVOKABLE void loadNewer();                 // page above the newest row
    Q_INVOKABLE void gotoTimestamp(qlonglong ts,
                                   const QString &source = "local");
    Q_INVOKABLE void resetToBottom();
    Q_INVOKABLE void send(const QString &body);
    Q_INVOKABLE void cullOld(int count);          // view dropped oldest rows
    Q_INVOKABLE void cullNew(int count);          // view dropped newest rows

    // Routing and transforms are public so tests can drive them directly.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);

    void applyBatch(const QVariantList &messages);
    void applyFields(qlonglong ts, const QVariantMap &fields);
    void applyConfirmed(qlonglong ts, qlonglong newTs,
                        const QString &serverStatus);
    void applyRetracted(qlonglong ts);
    void applyGotoSlice(const QVariantList &messages);

signals:
    void backendChanged();
    void accountChanged();
    void chatChanged();
    void groupchatChanged();
    void atTailChanged();
    void catchupBusyChanged();
    void loadingOlderChanged();
    // A history request finished; dir is init/old/new/goto/catchup and added is
    // the net rows inserted. The view pages off this to fill an under-tall
    // viewport, and stops when added == 0 (archive exhausted).
    void loaded(const QString &dir, int added);

private:
    void reload();
    void setAtTail(bool v);
    void setCatchupBusy(bool v);
    bool isMyCatchup(const QString &jid) const;
    void reconcileCatchup();
    int indexOfTs(qlonglong ts) const;
    int insertPos(qlonglong ts) const;
    void issueHistory(const QString &dir, qlonglong cursor, bool haveCursor);
    void cancelDir(const QString &dir);
    void markInflight(const QString &dir, bool busy);
    qlonglong oldestTs() const;
    qlonglong newestTs() const;

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_chat;
    bool m_groupchat = false;
    bool m_atTail = true;       // an empty window is vacuously at tail
    bool m_catchupBusy = false;
    qlonglong m_tailTs = 0;     // newest real-message ts, from message <Tail>

    QList<QVariantMap> m_msgs;  // row 0 = newest

    QHash<int, QString> m_pending; // token -> dir
    QSet<QString> m_inflight;
};

#endif // CHATMODEL_H
