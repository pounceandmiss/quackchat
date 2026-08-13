#include "ChatSession.h"

ChatSession::ChatSession(TackyBackend *backend, const QString &acc,
                         const QString &jid, bool groupchat, QObject *parent)
    : QObject(parent) {
    // Backend last: the account and chat are what the model needs before it will
    // ask for anything, and setting it first sends off an initial load for an
    // empty chat.
    m_messages.setAccount(acc);
    m_messages.setChat(jid);
    m_messages.setGroupchat(groupchat);
    m_messages.setBackend(backend);

    m_omemo.setAccount(acc);
    m_omemo.setJid(jid);
    m_omemo.setGroupchat(groupchat);
    m_omemo.setBackend(backend);
}

void ChatSession::setDraft(const QString &text) {
    if (m_draft == text)
        return;
    m_draft = text;
    emit draftChanged();
}

void ChatSession::replyToMessage(qlonglong ts, const QString &body,
                                 bool outgoing) {
    m_replyTo = ts;
    m_replyBody = body;
    m_replyOutgoing = outgoing;
    emit replyChanged();
}

void ChatSession::cancelReply() {
    if (m_replyTo == 0)
        return;
    m_replyTo = 0;
    m_replyBody.clear();
    m_replyOutgoing = false;
    emit replyChanged();
}

void ChatSession::sendDraft() {
    const QString body = m_draft.trimmed();
    if (body.isEmpty())
        return;
    m_messages.send(body, m_replyTo);
    setDraft(QString());
    cancelReply();
}
