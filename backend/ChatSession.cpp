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

void ChatSession::setGroupchat(bool v) {
    m_messages.setGroupchat(v);
    m_omemo.setGroupchat(v);
}

void ChatSession::setDraft(const QString &text) {
    if (m_draft == text)
        return;
    m_draft = text;
    emit draftChanged();
}

void ChatSession::replyToMessage(qlonglong ts, const QString &body,
                                 bool outgoing) {
    // The composer does one or the other, never both.
    cancelEdit();
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

void ChatSession::editMessage(qlonglong ts, const QString &body) {
    if (ts == 0)
        return;
    cancelReply();
    // One edit straight into another keeps the first stash, which is still the
    // draft rather than the message being left.
    if (!m_stashed) {
        m_stashedDraft = m_draft;
        m_stashed = true;
    }
    m_editing = ts;
    emit editChanged();
    setDraft(body);
}

void ChatSession::cancelEdit() {
    if (m_editing == 0)
        return;
    m_editing = 0;
    restoreStash();
    emit editChanged();
}

void ChatSession::restoreStash() {
    if (!m_stashed)
        return;
    const QString back = m_stashedDraft;
    m_stashedDraft.clear();
    m_stashed = false;
    setDraft(back);
}

void ChatSession::sendDraft() {
    const QString body = m_draft.trimmed();
    if (body.isEmpty())
        return;
    if (m_editing != 0) {
        m_messages.edit(m_editing, body);
        cancelEdit();
        return;
    }
    m_messages.send(body, m_replyTo);
    setDraft(QString());
    cancelReply();
}
