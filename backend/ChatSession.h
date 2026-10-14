// Everything one conversation holds that outlives a view of it: the message
// window, the OMEMO switch, and the message half-written into the composer.
//
// Cached per (account, jid) on AppController, so the shell and a pop-out window
// showing the same chat share one of these rather than each building its own -
// two ChatModels on one chat means two paging cursors and two MAM round trips
// for the same history. What stays in ChatPage is what belongs to a view of the
// conversation rather than the conversation: the selection and an in-chat
// search. The scroll position is kept here so it survives switching chats, and
// in tacky across runs.
#ifndef CHATSESSION_H
#define CHATSESSION_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ChatModel.h"
#include "OmemoChat.h"
#include "TackyBackend.h"

class ChatSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Obtained from App.chatFor()")
    Q_PROPERTY(ChatModel *messages READ messages CONSTANT)
    Q_PROPERTY(OmemoChat *omemo READ omemo CONSTANT)
    // Typed but not sent. Here rather than in the composer so that leaving a
    // chat and coming back finds it again, and so that it does not follow you
    // into somebody else's conversation.
    Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
    // The preview of the message being answered, which the composer draws above
    // the field. Its timestamp stays in here: the only thing that needs it is
    // the send, which happens in here too.
    Q_PROPERTY(QString replyBody READ replyBody NOTIFY replyChanged)
    Q_PROPERTY(bool replyOutgoing READ replyOutgoing NOTIFY replyChanged)
    Q_PROPERTY(bool replying READ replying NOTIFY replyChanged)
    // Files chosen but not sent yet, which the composer shows in a tray over
    // the field. One map per file: `url` (a local file), `name`, `isImage`.
    // Here with the draft, and kept here for the same reason.
    Q_PROPERTY(QVariantList pending READ pending NOTIFY pendingChanged)
    // Whether the composer is correcting a message rather than writing one. Its
    // timestamp stays in here for the reason the reply's does: only the send
    // needs it.
    Q_PROPERTY(bool editing READ editing NOTIFY editChanged)
    // Where the feed was left: the bottom row, and its bottom edge's offset
    // above the view's in px. viewTs 0 is the newest edge. viewLoaded: tacky
    // has answered.
    Q_PROPERTY(qlonglong viewTs READ viewTs NOTIFY viewChanged)
    Q_PROPERTY(qreal viewOffset READ viewOffset NOTIFY viewChanged)
    Q_PROPERTY(bool viewLoaded READ viewLoaded NOTIFY viewChanged)

public:
    ChatSession(TackyBackend *backend, const QString &acc, const QString &jid,
                bool groupchat, QObject *parent = nullptr);
    ~ChatSession() override;

    ChatModel *messages() { return &m_messages; }
    OmemoChat *omemo() { return &m_omemo; }

    void setGroupchat(bool v);

    QString draft() const { return m_draft; }
    void setDraft(const QString &text);

    QString replyBody() const { return m_replyBody; }
    bool replyOutgoing() const { return m_replyOutgoing; }
    bool replying() const { return m_replyTo != 0; }

    bool editing() const { return m_editing != 0; }

    QVariantList pending() const { return m_pending; }

    qlonglong viewTs() const { return m_viewTs; }
    qreal viewOffset() const { return m_viewOffset; }
    bool viewLoaded() const { return m_viewLoaded; }
    // Per scroll; only flush() writes it to tacky.
    Q_INVOKABLE void setView(qlonglong ts, qreal offset);
    // Write to tacky if changed. On the last release, on destruction, and when
    // the app leaves the foreground.
    void flush();

    // Queue a file, or drop it if there is nothing readable behind it.
    // Resolved to a local path now rather than at send time: on Android a
    // dialog hands back a content:// url readable only while the grant
    // lasts, and the tray has a thumbnail to draw before then.
    Q_INVOKABLE void attach(const QUrl &file);
    Q_INVOKABLE void unattach(int index);

    // Mention someone in a room: "nick: " into an empty draft, otherwise the
    // nick appended to it.
    Q_INVOKABLE void mention(const QString &nick);

    Q_INVOKABLE void replyToMessage(qlonglong ts, const QString &body,
                                    bool outgoing);
    Q_INVOKABLE void cancelReply();

    // Put a message back in the composer to correct it. An edit fills the
    // field, so the draft it displaces is kept and comes back after either.
    Q_INVOKABLE void editMessage(qlonglong ts, const QString &body);
    Q_INVOKABLE void cancelEdit();

    // Send the queue and then the draft, clearing them and the reply along
    // with them. No-op when both are empty, which is what an empty composer
    // press means. While editing this corrects that message instead of
    // sending a new one, and the queue waits: an edit replaces words, and
    // there is nowhere on one to hang a file.
    Q_INVOKABLE void sendDraft();

    // One hold per page showing the chat. tacky gets `chat open` on the first
    // and `chat close` on the last.
    Q_INVOKABLE void hold();
    Q_INVOKABLE void release();

signals:
    void draftChanged();
    void pendingChanged();
    void replyChanged();
    void editChanged();
    void viewChanged();

private:
    void restoreStash();
    void sendOpen(bool open);
    void loadClientData();
    void handleResult(int token, const QVariant &data);
    void handleError(int token, const QString &message);

    // AppController destroys its backend member before its children.
    QPointer<TackyBackend> m_backend;
    QString m_acc;
    QString m_jid;
    int m_holds = 0;

    ChatModel m_messages;
    OmemoChat m_omemo;
    QString m_draft;
    QVariantList m_pending;
    qlonglong m_replyTo = 0;
    QString m_replyBody;
    bool m_replyOutgoing = false;
    qlonglong m_editing = 0;
    // What the edit displaced. Its own flag, since a stashed blank draft is
    // not the same as nothing stashed.
    QString m_stashedDraft;
    bool m_stashed = false;

    // The whole stored object, so keys we don't own survive a write.
    QJsonObject m_clientData;
    int m_clientDataToken = 0;
    bool m_clientDataDirty = false;
    qlonglong m_viewTs = 0;
    qreal m_viewOffset = 0;
    bool m_viewLoaded = false;
};

#endif // CHATSESSION_H
