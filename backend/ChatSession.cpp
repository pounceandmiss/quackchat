#include "ChatSession.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>

#include "PickedFile.h"

ChatSession::ChatSession(TackyBackend *backend, const QString &acc,
                         const QString &jid, bool groupchat, QObject *parent)
    : QObject(parent), m_backend(backend), m_acc(acc), m_jid(jid) {
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

    // A reattached backend (Android's service) may not know the chat is open.
    if (backend) {
        connect(backend, &TackyBackend::connected, this, [this] {
            if (m_holds > 0)
                sendOpen(true);
            // A load sent while unattached was lost.
            if (!m_viewLoaded)
                loadClientData();
        });
        connect(backend, &TackyBackend::result, this, &ChatSession::handleResult);
        connect(backend, &TackyBackend::error, this, &ChatSession::handleError);
    }
    loadClientData();
}

ChatSession::~ChatSession() {
    flush();
    if (m_holds > 0)
        sendOpen(false);
}

void ChatSession::loadClientData() {
    if (!m_backend) {
        m_viewLoaded = true;
        return;
    }
    m_clientDataToken = m_backend->request(
        QStringLiteral("chat"), QStringLiteral("clientData"),
        QVariantMap{{QStringLiteral("acc"), m_acc},
                    {QStringLiteral("chat"), m_jid}});
}

void ChatSession::handleResult(int token, const QVariant &data) {
    if (token == 0 || token != m_clientDataToken)
        return;
    m_clientDataToken = 0;
    const QJsonObject stored =
        QJsonDocument::fromJson(data.toString().toUtf8()).object();
    // A scroll made before the answer wins over the stored view.
    const QJsonValue view = m_clientDataDirty
                                ? m_clientData.value(QLatin1String("view"))
                                : stored.value(QLatin1String("view"));
    m_clientData = stored;
    if (view.isUndefined())
        m_clientData.remove(QLatin1String("view"));
    else
        m_clientData.insert(QLatin1String("view"), view);
    if (!m_clientDataDirty) {
        const QJsonObject v = view.toObject();
        m_viewTs = v.value(QLatin1String("ts")).toInteger();
        m_viewOffset = v.value(QLatin1String("offset")).toDouble();
    }
    m_viewLoaded = true;
    emit viewChanged();
}

// A failed read counts as nothing stored.
void ChatSession::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token == 0 || token != m_clientDataToken)
        return;
    m_clientDataToken = 0;
    // Unsent: the connected handler retries.
    if (m_viewLoaded || !m_backend || !m_backend->isRunning())
        return;
    m_viewLoaded = true;
    emit viewChanged();
}

void ChatSession::setView(qlonglong ts, qreal offset) {
    if (ts == m_viewTs && offset == m_viewOffset)
        return;
    m_viewTs = ts;
    m_viewOffset = ts == 0 ? 0 : offset;
    if (ts == 0)
        m_clientData.remove(QLatin1String("view"));
    else
        m_clientData.insert(QLatin1String("view"),
                            QJsonObject{{QLatin1String("ts"), ts},
                                        {QLatin1String("offset"), m_viewOffset}});
    m_clientDataDirty = true;
    emit viewChanged();
}

void ChatSession::flush() {
    if (!m_clientDataDirty || !m_backend)
        return;
    m_clientDataDirty = false;
    // An empty object is sent as "", which deletes the row.
    const QString data = m_clientData.isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(m_clientData).toJson(QJsonDocument::Compact));
    m_backend->notify(QStringLiteral("chat"), QStringLiteral("setClientData"),
                      QVariantMap{{QStringLiteral("acc"), m_acc},
                                  {QStringLiteral("chat"), m_jid},
                                  {QStringLiteral("data"), data}});
}

void ChatSession::hold() {
    if (m_holds++ == 0)
        sendOpen(true);
}

void ChatSession::release() {
    if (m_holds == 0)
        return;
    if (--m_holds == 0) {
        flush();
        sendOpen(false);
    }
}

void ChatSession::sendOpen(bool open) {
    if (!m_backend)
        return;
    if (open)
        m_messages.forgetMarkedRead();
    const QVariantMap args{{QStringLiteral("acc"), m_acc},
                           {QStringLiteral("chat"), m_jid}};
    m_backend->notify(QStringLiteral("chat"),
                      open ? QStringLiteral("open") : QStringLiteral("close"),
                      args);
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

void ChatSession::mention(const QString &nick) {
    if (nick.isEmpty())
        return;
    if (m_draft.trimmed().isEmpty())
        setDraft(nick + QStringLiteral(": "));
    else
        setDraft(m_draft + (m_draft.back().isSpace() ? QString() : QStringLiteral(" "))
                 + nick + QLatin1Char(' '));
}

void ChatSession::attach(const QUrl &file) {
    const QString path = pickedfile::localPath(file);
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    // Read off the file, not the name: the tray draws an image as a
    // thumbnail and everything else as a chip, as the feed does.
    const bool isImage = QMimeDatabase()
                             .mimeTypeForFile(info)
                             .name()
                             .startsWith(QLatin1String("image/"));
    m_pending.append(QVariantMap{{QStringLiteral("url"), QUrl::fromLocalFile(path)},
                                 {QStringLiteral("name"), info.fileName()},
                                 {QStringLiteral("isImage"), isImage}});
    emit pendingChanged();
}

void ChatSession::unattach(int index) {
    if (index < 0 || index >= m_pending.size())
        return;
    m_pending.removeAt(index);
    emit pendingChanged();
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
    if (m_editing != 0) {
        if (body.isEmpty())
            return;
        m_messages.edit(m_editing, body);
        cancelEdit();
        return;
    }
    if (body.isEmpty() && m_pending.isEmpty())
        return;
    // One message per file, in the order they were queued: tacky attaches a
    // single file to a message, so a queue of three is three rows.
    for (const QVariant &file : std::as_const(m_pending))
        m_messages.sendFile(file.toMap().value(QStringLiteral("url")).toUrl());
    if (!m_pending.isEmpty()) {
        m_pending.clear();
        emit pendingChanged();
    }
    if (!body.isEmpty())
        m_messages.send(body, m_replyTo);
    setDraft(QString());
    // Only the words can carry the reply, but the banner goes either way:
    // left up, it would thread the next message instead.
    cancelReply();
}
