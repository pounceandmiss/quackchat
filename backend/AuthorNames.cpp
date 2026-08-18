#include "AuthorNames.h"

#include "BackendBinding.h"

AuthorNames::AuthorNames(QObject *parent) : QObject(parent) {}

void AuthorNames::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);
    m_backend = backend;
    if (m_backend)
        bindBackend(this, m_backend, &AuthorNames::refresh);
    emit backendChanged();
    refresh();
}

void AuthorNames::setAccount(const QString &acc) {
    if (m_account == acc)
        return;
    m_account = acc;
    emit accountChanged();
    refresh();
}

void AuthorNames::setChat(const QString &chat) {
    if (m_chat == chat)
        return;
    m_chat = chat;
    emit chatChanged();
    refresh();
}

// One map per chat, so the old one goes as soon as the chat does - a stale
// name under a new conversation would be worse than a bare JID.
void AuthorNames::refresh() {
    m_pending = 0;
    if (!m_names.isEmpty()) {
        m_names.clear();
        emit namesChanged();
    }
    if (!m_backend || m_account.isEmpty() || m_chat.isEmpty())
        return;
    m_pending = m_backend->request(
        QStringLiteral("author"), QStringLiteral("get"),
        QVariantMap{{QStringLiteral("acc"), m_account},
                    {QStringLiteral("chat"), m_chat}});
}

void AuthorNames::handleResult(int token, const QVariant &data) {
    if (token != m_pending)
        return;
    m_pending = 0;
    m_names = data.toMap();
    emit namesChanged();
}

// <Changed> re-resolves one sender in place: a roster edit, a nick change, a
// newly arrived occupant. Refetching the whole map for one of them is waste.
// The names stay as they were; the next connected edge asks again.
void AuthorNames::handleError(int token, const QString &message) {
    Q_UNUSED(message)
    if (token == m_pending)
        m_pending = 0;
}

void AuthorNames::handleEvent(const QString &module, const QString &name,
                              const QVariant &args) {
    // Occupants are rebuilt when the session comes up, so whatever we resolved
    // against the old one is stale and <Changed> won't replay it.
    if (module == QLatin1String("conn") && name == QLatin1String("Ready")) {
        if (args.toMap().value(QStringLiteral("acc")).toString() == m_account)
            refresh();
        return;
    }
    if (module != QLatin1String("author") || name != QLatin1String("Changed"))
        return;
    const QVariantMap a = args.toMap();
    if (a.value(QStringLiteral("acc")).toString() != m_account ||
        a.value(QStringLiteral("chat")).toString() != m_chat)
        return;
    const QString from = a.value(QStringLiteral("from")).toString();
    if (from.isEmpty())
        return;
    m_names.insert(from, a.value(QStringLiteral("name")).toString());
    emit namesChanged();
}
