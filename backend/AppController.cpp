#include "AppController.h"

AppController::AppController(QObject *parent) : QObject(parent) {
    m_accounts.setBackend(&m_backend);
    m_avatars.setBackend(&m_backend);
}

ChatListModel *AppController::chatListFor(const QString &acc) {
    if (acc.isEmpty())
        return nullptr;
    ChatListModel *&m = m_chatLists[acc];
    if (!m) {
        m = new ChatListModel(this);
        m->setBackend(&m_backend);
        m->setAccount(acc);
    }
    return m;
}

AccountSettings *AppController::accountSettingsFor(const QString &acc) {
    if (acc.isEmpty())
        return nullptr;
    AccountSettings *&s = m_accountSettings[acc];
    if (!s) {
        s = new AccountSettings(this);
        s->setBackend(&m_backend);
        s->setAccount(acc);
    }
    return s;
}

void AppController::startFromEnvironment() {
    if (m_started)
        return;
    m_started = true;

    // Persist to disk so an enabled account reconnects next launch without the
    // env vars. No -config-dir override, so we share tacky's own store
    // (~/.config/tacky) rather than keeping a separate quackchat one.
    m_backend.start({QStringLiteral("-transient"), QStringLiteral("0")});

    const QString acc = qEnvironmentVariable("TACKY_ACC");
    if (!acc.isEmpty()) {
        m_backend.notify(
            QStringLiteral("account"), QStringLiteral("add"),
            QVariantMap{{QStringLiteral("acc"), acc},
                        {QStringLiteral("password"),
                         qEnvironmentVariable("TACKY_PASSWORD")}});
        m_backend.notify(QStringLiteral("account"), QStringLiteral("enable"),
                         QVariantMap{{QStringLiteral("acc"), acc}});
    }

    // Accounts already on disk auto-connect but never re-emit <Added>, so the
    // rail only sees them if we enumerate.
    m_accounts.refresh();
}
