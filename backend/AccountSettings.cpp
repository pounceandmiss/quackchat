#include "AccountSettings.h"

#include "BackendBinding.h"

#include "AvatarEncoder.h"
#include "TackyBackend.h"

AccountSettings::AccountSettings(QObject *parent) : QObject(parent) {}

void AccountSettings::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    m_devices.setAccount(acc);
    // The page shows this account's own keys, so it is its own subject.
    m_devices.setJid(acc);
    emit accountChanged();
    refresh();
}

void AccountSettings::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    m_devices.setBackend(backend);
    if (m_backend)
        bindBackend(this, m_backend, &AccountSettings::refresh);
    refresh();
}

void AccountSettings::refresh() {
    if (!m_backend || m_account.isEmpty())
        return;
    m_getToken =
        m_backend->request(QStringLiteral("account"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("acc"), m_account}});
    requestNick();
}

// Our own nick, so the subject is the account itself.
void AccountSettings::requestNick() {
    m_nickGetToken =
        m_backend->request(QStringLiteral("nick"), QStringLiteral("get"),
                           QVariantMap{{QStringLiteral("acc"), m_account},
                                       {QStringLiteral("jid"), m_account}});
}

void AccountSettings::save(const QString &password, const QString &nick) {
    if (!m_backend || m_account.isEmpty())
        return;

    bool wrote = false;
    if (password != m_password) {
        m_backend->notify(QStringLiteral("account"), QStringLiteral("add"),
                          QVariantMap{{QStringLiteral("acc"), m_account},
                                      {QStringLiteral("password"), password}});
        m_password = password;
        emit passwordChanged();
        wrote = true;
    }

    if (nick != m_nick) {
        // Publishing the nickname is a round trip to the server, so this is
        // the one write worth waiting on before calling the save done.
        m_nickToken =
            m_backend->request(QStringLiteral("nick"), QStringLiteral("set"),
                               QVariantMap{{QStringLiteral("acc"), m_account},
                                           {QStringLiteral("nick"), nick}});
        setStatus(QStringLiteral("Saving"), false);
        emit savingChanged();
        return;
    }

    setStatus(wrote ? QStringLiteral("Saved") : QString(), false);
    emit saved();
}

void AccountSettings::setAvatar(const QUrl &source) {
    if (!m_backend || m_account.isEmpty() || m_avatarToken >= 0)
        return;
    if (!m_encoder) {
        setAvatarStatus(QStringLiteral("Cannot read pictures here"), true);
        return;
    }

    // Decoding is synchronous, and so is the report if it fails: nothing has
    // been sent yet, so there is no round-trip to wait out.
    QString error;
    const AvatarImage image = m_encoder->encode(source, &error);
    if (image.png.isEmpty()) {
        setAvatarStatus(error.isEmpty() ? QStringLiteral("Could not read that picture")
                                        : error,
                        true);
        return;
    }

    m_avatarToken = m_backend->request(
        QStringLiteral("avatar"), QStringLiteral("publish"),
        QVariantMap{
            {QStringLiteral("acc"), m_account},
            // The transport is JSON, which is text, so tacky declares this
            // argument base64 and decodes it before publishing. A QByteArray
            // here would be UTF-8 decoded on the way into the JSON document.
            {QStringLiteral("data"), QString::fromLatin1(image.png.toBase64())},
            {QStringLiteral("type"), QStringLiteral("image/png")},
            {QStringLiteral("width"), image.width},
            {QStringLiteral("height"), image.height}});
    setAvatarStatus(QStringLiteral("Publishing"), false);
    emit avatarBusyChanged();
}

void AccountSettings::clearAvatar() {
    if (!m_backend || m_account.isEmpty() || m_avatarToken >= 0)
        return;
    m_avatarToken =
        m_backend->request(QStringLiteral("avatar"), QStringLiteral("disable"),
                           QVariantMap{{QStringLiteral("acc"), m_account}});
    setAvatarStatus(QStringLiteral("Removing"), false);
    emit avatarBusyChanged();
}

// Both calls drop their own cached copy and emit `avatar <Update>`, so there is
// nothing to undo here on the way out - the picture on screen follows on its
// own.
void AccountSettings::finishAvatar(const QString &error) {
    m_avatarToken = -1;
    // Success says nothing: the picture itself is the confirmation.
    setAvatarStatus(error, !error.isEmpty());
    emit avatarBusyChanged();
}

void AccountSettings::handleEvent(const QString &module, const QString &name,
                                  const QVariant &args) {
    if (!m_backend)
        return;
    // tacky narrates a publish in two steps (data, then metadata), which is the
    // only progress there is to show while the upload is out.
    if (module == QLatin1String("avatar") && name == QLatin1String("Progress")) {
        const QVariantMap a = args.toMap();
        if (m_avatarToken >= 0 &&
            a.value(QStringLiteral("acc")).toString() == m_account)
            setAvatarStatus(a.value(QStringLiteral("message")).toString(), false);
        return;
    }
    // The nick is server state, so a fresh session may carry a different one
    // than the reply we got against the old one.
    if (sessionUp(module, name, args, m_account)) {
        requestNick();
        return;
    }
    if (module != QLatin1String("nick") || name != QLatin1String("Changed"))
        return;
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account)
        return;
    // <Changed> carries the JID, not the nick; the cache holds the new value.
    if (a.value(QStringLiteral("jid")).toString() != m_account)
        return;
    requestNick();
}

void AccountSettings::handleResult(int token, const QVariant &data) {
    if (token == m_getToken) {
        applyAccount(data.toMap());
    } else if (token == m_nickGetToken) {
        applyNick(data.toString());
    } else if (token == m_nickToken) {
        m_nickToken = -1;
        emit savingChanged();
        setStatus(QStringLiteral("Saved"), false);
        emit saved();
    } else if (token == m_avatarToken) {
        finishAvatar(QString()); // publish/disable answer empty on success
    }
}

// A failed read (no such account yet, or no backend) leaves the fields alone
// rather than blanking what the user typed, so only the write is reported.
void AccountSettings::handleError(int token, const QString &message) {
    if (token == m_avatarToken) {
        // Where a rejected publish lands: the server's reason, a dispatch that
        // threw before sending, or the request timing out unanswered.
        finishAvatar(message.isEmpty() ? QStringLiteral("Could not publish the picture")
                                       : message);
        return;
    }
    if (token != m_nickToken)
        return;
    m_nickToken = -1;
    emit savingChanged();
    setStatus(message, true);
}

void AccountSettings::applyAccount(const QVariantMap &row) {
    const QString password = row.value(QStringLiteral("password")).toString();
    if (password == m_password)
        return;
    m_password = password;
    emit passwordChanged();
}

void AccountSettings::applyNick(const QString &nick) {
    if (nick == m_nick)
        return;
    m_nick = nick;
    emit nickChanged();
}

void AccountSettings::setStatus(const QString &text, bool error) {
    if (m_status == text && m_statusError == error)
        return;
    m_status = text;
    m_statusError = error;
    emit statusChanged();
}

void AccountSettings::setAvatarStatus(const QString &text, bool error) {
    if (m_avatarStatus == text && m_avatarError == error)
        return;
    m_avatarStatus = text;
    m_avatarError = error;
    emit avatarStatusChanged();
}
