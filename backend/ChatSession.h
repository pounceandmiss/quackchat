// Everything one conversation holds that outlives a view of it: the message
// window, the OMEMO switch, and the message half-written into the composer.
//
// Cached per (account, jid) on AppController, so the shell and a pop-out window
// showing the same chat share one of these rather than each building its own -
// two ChatModels on one chat means two paging cursors and two MAM round trips
// for the same history. What stays in ChatPage is what belongs to a view of the
// conversation rather than the conversation: the scroll position, the selection,
// and an in-chat search.
#ifndef CHATSESSION_H
#define CHATSESSION_H

#include <QObject>
#include <QString>
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

public:
    ChatSession(TackyBackend *backend, const QString &acc, const QString &jid,
                bool groupchat, QObject *parent = nullptr);

    ChatModel *messages() { return &m_messages; }
    OmemoChat *omemo() { return &m_omemo; }

    QString draft() const { return m_draft; }
    void setDraft(const QString &text);

    QString replyBody() const { return m_replyBody; }
    bool replyOutgoing() const { return m_replyOutgoing; }
    bool replying() const { return m_replyTo != 0; }

    Q_INVOKABLE void replyToMessage(qlonglong ts, const QString &body,
                                    bool outgoing);
    Q_INVOKABLE void cancelReply();

    // Send the draft, clearing it and the reply along with it. No-op when the
    // draft is blank, which is also what an empty composer press means.
    Q_INVOKABLE void sendDraft();

signals:
    void draftChanged();
    void replyChanged();

private:
    ChatModel m_messages;
    OmemoChat m_omemo;
    QString m_draft;
    qlonglong m_replyTo = 0;
    QString m_replyBody;
    bool m_replyOutgoing = false;
};

#endif // CHATSESSION_H
