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
#include <QUrl>
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
    // A page of older history is out: a `before` fill that comes up short
    // locally reaches for the archive, and can be out a while. The view shows
    // this rather than looking idle.
    Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY loadingOlderChanged)
    // Why the last page failed, or "" if it did not. A failed page leaves the
    // window as short as an exhausted archive does, so the view is told which.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged)
    // Whether the account has a stream. Without one a page is buffered by
    // tacky until there is, so the feed is waiting rather than working.
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    // CSS color for quoted runs in MarkupRole. A QString rather than a QColor:
    // it goes straight into the markup, and QColor would pull QtGui in here.
    Q_PROPERTY(QString quoteColor READ quoteColor WRITE setQuoteColor NOTIFY quoteColorChanged)
    // And for the run a search matched, marked by highlightMatches().
    Q_PROPERTY(QString matchColor READ matchColor WRITE setMatchColor NOTIFY matchColorChanged)
    // The long side, in device pixels, of the thumbnails tacky is asked for.
    // The view sets it, since only it knows its screen's ratio.
    Q_PROPERTY(int thumbMax READ thumbMax WRITE setThumbMax NOTIFY thumbMaxChanged)

public:
    enum Role {
        TimestampRole = Qt::UserRole + 1,
        BodyRole,        // text body or media caption, from the content union
        MarkupRole,      // BodyRole as rich text; empty when it needs none
        AttachmentsRole, // the union's attachment list, each merged with the
                         // state of its transfer; empty for a text message
        OutgoingRole,
        ServerStatusRole, // the hop to our own server
        RemoteStatusRole, // and the hop after it: none/delivered/read
        FromRole,
        RetractedRole,   // tombstone: render the deleted-message placeholder
        ReactionsRole,   // aggregated map: emoji -> {reactors, mine}
        ReplyBodyRole,   // one-line preview of the message this one answers
        ReplyAuthorRole, // and who wrote it; both empty when this is no reply
        EncryptionRole,  // "omemo" when the row is OMEMO, "" for cleartext
        FailReasonRole,  // why a failed row failed: encrypt, delivery, or ""
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
    QString loadError() const { return m_loadError; }
    bool online() const { return m_online; }
    QString quoteColor() const { return m_quoteColor; }
    QString matchColor() const { return m_matchColor; }
    int thumbMax() const { return m_thumbMax; }
    void setBackend(TackyBackend *backend);
    void setAccount(const QString &acc);
    void setChat(const QString &chat);
    void setGroupchat(bool v);
    void setQuoteColor(const QString &css);
    void setMatchColor(const QString &css);
    void setThumbMax(int px);

    // Mark where a search matched inside one message, so the row shows which
    // characters were found and not merely that it was. `ranges` is tacky's
    // own content.matches; an empty one, or ts 0, takes the mark away.
    Q_INVOKABLE void highlightMatches(qlonglong ts, const QVariantList &ranges);

    Q_INVOKABLE void loadInitial();               // newest page (no cursor)
    Q_INVOKABLE void loadOlder();                 // page below the oldest row
    Q_INVOKABLE void loadNewer();                 // page above the newest row
    // Ask again for whatever last failed; the automatic fill will not.
    Q_INVOKABLE void retry();
    Q_INVOKABLE void gotoTimestamp(qlonglong ts,
                                   const QString &source = "local");
    // Jump to whatever the message at `ts` answered. A target the store cannot
    // resolve comes back empty and leaves the window alone.
    Q_INVOKABLE void gotoReplyTarget(qlonglong ts);
    Q_INVOKABLE int rowOfTimestamp(qlonglong ts) const { return indexOfTs(ts); }
    // Ask tacky for the stanza it recorded for a message; rawXmlReady carries
    // the answer, laid out, under the token this returns. 0 when there was
    // nothing to ask, where no answer is coming.
    Q_INVOKABLE int requestRawXml(qlonglong ts);
    Q_INVOKABLE void resetToBottom();
    // Advance our read watermark to the newest row. tacky has no "the user is
    // looking at this chat" call - this is the gate that stops notify <Notify>
    // firing for a chat on screen, so the view calls it whenever it is.
    Q_INVOKABLE void markRead();
    // replyToTs names the message being answered, 0 for a plain send.
    Q_INVOKABLE void send(const QString &body, qlonglong replyToTs = 0);
    // The row shows at once with the local path standing in for the url; the
    // PUT and the send that follows it are tacky's.
    Q_INVOKABLE void sendFile(const QUrl &file);
    // For a row whose file never went up, where resend() has nothing to send.
    Q_INVOKABLE void retryUpload(qlonglong ts);
    // Toggles one emoji in our own set for that message, per XEP-0444; the
    // same call again takes it back.
    // Another go at a message that did not get out. plaintext drops this one
    // row's encryption; the chat's own switch is left alone.
    Q_INVOKABLE void resend(qlonglong ts, bool plaintext = false);
    Q_INVOKABLE void react(qlonglong ts, const QString &emoji);
    Q_INVOKABLE void reactClear(qlonglong ts);
    Q_INVOKABLE void cullOld(int count);          // view dropped oldest rows
    Q_INVOKABLE void cullNew(int count);          // view dropped newest rows

    // Fetch an attachment the autofetch policy held back, or one whose transfer
    // failed. Ungated: the user asked for this one.
    Q_INVOKABLE void loadAttachment(qlonglong ts, int idx);
    // Resolve an attachment to a file on disk, downloading it if need be, and
    // report where it landed through attachmentResolved.
    Q_INVOKABLE void openAttachment(qlonglong ts, int idx);
    // The same resolve, then a copy at `dest` or the folder it sits in.
    Q_INVOKABLE void saveAttachment(qlonglong ts, int idx, const QUrl &dest);
    Q_INVOKABLE void revealAttachment(qlonglong ts, int idx);
    // Either direction; the transfer ends `idle`.
    Q_INVOKABLE void cancelAttachment(qlonglong ts, int idx);
    // Deletes the downloaded copy and its thumbnail, leaving the row offering
    // the fetch again.
    Q_INVOKABLE void uncacheAttachment(qlonglong ts, int idx);

    // Routing and transforms are public so tests can drive them directly.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

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
    void loadErrorChanged();
    void onlineChanged();
    void quoteColorChanged();
    void matchColorChanged();
    void thumbMaxChanged();
    // A history request finished; dir is init/old/new/goto/catchup and added is
    // the net rows inserted. The view pages off this to fill an under-tall
    // viewport, and stops when added == 0 (archive exhausted).
    void loaded(const QString &dir, int added);
    // A jump settled on `ts`, which the view scrolls to; 0 when the target
    // could not be resolved and nothing moved.
    void anchored(qlonglong ts);
    // Where an attachment the user asked to open ended up, or empty when it
    // could not be fetched. Opening it is the view's job: that needs QtGui.
    void attachmentResolved(const QUrl &url);
    void attachmentFolder(const QUrl &url);
    // `error` is empty when the copy landed.
    void attachmentSaved(const QUrl &dest, const QString &error);
    // What requestRawXml asked for, under the token it answers - every window
    // on a chat shares one model, so that is what tells them apart. Empty for
    // a message that never had a stanza built, and for a request the backend
    // refused: the viewer's empty state covers both.
    void rawXmlReady(int token, const QString &xml);

private:
    // What a resolved download was wanted for: the path it answers with says
    // nothing about that.
    struct PendingAction {
        enum Kind { Open, Folder, Save } kind = Open;
        QUrl dest; // Save only
    };

    void reload();
    void setAtTail(bool v);
    void setCatchupBusy(bool v);
    bool isMyCatchup(const QString &jid) const;
    void reconcileCatchup();
    int indexOfTs(qlonglong ts) const;
    void issueGoto(const QString &method, const QVariantMap &args);
    int insertPos(qlonglong ts) const;
    void issueHistory(const QString &dir, qlonglong cursor, bool haveCursor);
    void pullConnState();
    void setLoadError(const QString &message);
    void setOnline(bool v);
    void clearFailure(const QString &dir);
    void cancelDir(const QString &dir);
    void cancelAllDirs();
    void markInflight(const QString &dir, bool busy);
    qlonglong oldestTs() const;
    qlonglong newestTs() const;
    void handleFileUpdate(const QString &name, const QVariantMap &a);
    QVariantList attachmentsOf(const QVariantMap &m) const;
    QVariantList marksOn(const QVariantMap &m) const;
    QVariantMap attachmentAt(qlonglong ts, int idx) const;
    void fetchThumbs(const QVariantMap &msg);
    void redrawRowsUsing(const QString &key);
    void redrawRow(qlonglong ts);
    // Answers straight away when the file is already on disk.
    void resolveAttachment(qlonglong ts, int idx, const PendingAction &act);
    void finishAction(const PendingAction &act, const QString &path);

    TackyBackend *m_backend = nullptr;
    QString m_account;
    QString m_chat;
    bool m_groupchat = false;
    // Until QML binds the palette's, and what the Tk client uses verbatim.
    QString m_quoteColor = QStringLiteral("green");
    QString m_matchColor = QStringLiteral("yellow");
    // Theme.thumbSize unscaled, until the view pushes its own.
    int m_thumbMax = 320;
    // The one message carrying a search mark, and where in it. Beside the rows
    // rather than in them: it belongs to the search, not to the message.
    qlonglong m_matchTs = 0;
    QVariantList m_matchRanges;
    bool m_atTail = true;       // an empty window is vacuously at tail
    bool m_catchupBusy = false;
    qlonglong m_tailTs = 0;     // newest real-message ts, from message <Tail>
    qlonglong m_markedRead = 0; // highest ts already sent to markOwnRead

    QList<QVariantMap> m_msgs;  // row 0 = newest

    QHash<int, QString> m_pending; // token -> dir
    QSet<QString> m_inflight;
    // Directions whose last request errored. Gates the automatic fill; a page
    // that lands, a reload, a jump or a reconnect all clear it.
    QSet<QString> m_failed;
    QString m_loadError;
    bool m_online = false;

    // Transfer state, which lives beside the rows rather than in them: one
    // download serves every message quoting the same URL. Dropped on reload -
    // tacky answers a repeat request from its own cache.
    QHash<QString, QVariantMap> m_xfer;   // url -> transfer fields
    // Uploads key on the message instead: until the PUT is through the row
    // carries a local path, which no other row shares.
    QHash<qlonglong, QVariantMap> m_upload; // ts -> transfer fields
    QHash<int, PendingAction> m_pendingAction;
    QSet<int> m_xmlPending; // tokens of outstanding `message rawxml` requests
};

#endif // CHATMODEL_H
