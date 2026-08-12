#include "NotificationController.h"

#include <QVariantMap>

#include "Notifier.h"
#include "TackyBackend.h"

namespace {

// A JID cannot contain a control character, so this cannot collide with one.
const QChar kKeySep = QChar(0x1f);

QString keyFor(const QString &acc, const QString &jid) {
    return acc + kKeySep + jid;
}

} // namespace

NotificationController::NotificationController(QObject *parent)
    : QObject(parent) {}

void NotificationController::setBackend(TackyBackend *backend) {
    if (m_backend == backend)
        return;
    if (m_backend)
        m_backend->disconnect(this);

    m_backend = backend;
    if (!m_backend)
        return;

    connect(m_backend, &TackyBackend::event, this,
            &NotificationController::handleEvent);
    connect(m_backend, &TackyBackend::runningChanged, this,
            &NotificationController::onRunningChanged);
}

void NotificationController::setNotifier(Notifier *notifier) {
    if (m_notifier == notifier)
        return;

    delete m_notifier;
    m_notifier = notifier;
    if (!m_notifier)
        return;

    m_notifier->setParent(this);
    connect(m_notifier, &Notifier::activated, this, [this](const QString &key) {
        const int sep = key.indexOf(kKeySep);
        if (sep < 0)
            return;
        emit activated(key.left(sep), key.mid(sep + 1));
    });
}

void NotificationController::handleEvent(const QString &module,
                                         const QString &name,
                                         const QVariant &args) {
    if (!m_notifier)
        return;

    const bool isNotify = module == QLatin1String("notify")
                          && name == QLatin1String("<Notify>");
    const bool isOwnRead = module == QLatin1String("message")
                           && name == QLatin1String("<OwnRead>");
    if (!isNotify && !isOwnRead)
        return;

    const QVariantMap a = args.toMap();
    const QString acc = a.value(QStringLiteral("acc")).toString();
    const QString jid = a.value(QStringLiteral("jid")).toString();
    if (acc.isEmpty() || jid.isEmpty())
        return;

    if (isOwnRead) {
        // The watermark moved - here, in another window, or on another device
        // - so whatever is on screen for this chat is stale.
        m_notifier->retract(keyFor(acc, jid));
        return;
    }

    Notifier::Alert alert;
    // Same fallback as Android's: an alert with a blank sender is worse than
    // one naming the JID.
    alert.nick = a.value(QStringLiteral("nick")).toString();
    if (alert.nick.isEmpty())
        alert.nick = jid;
    alert.body = a.value(QStringLiteral("body")).toString();
    alert.unread = a.value(QStringLiteral("unread")).toInt();
    alert.mention = a.value(QStringLiteral("mention")).toBool();
    m_notifier->show(keyFor(acc, jid), alert);
}

void NotificationController::onRunningChanged() {
    // No more events are coming to retract these, and what is on screen would
    // outlive the session that vouched for it being unread.
    if (m_notifier && m_backend && !m_backend->isRunning())
        m_notifier->retractAll();
}
