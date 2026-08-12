#include "FdoNotifier.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStringList>
#include <QVariantMap>

namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";
constexpr auto kIface = "org.freedesktop.Notifications";

// The key a daemon invokes when the popup itself is clicked, as opposed to a
// button on it. Registering it with a label is what gets us both.
const QLatin1String kDefaultAction("default");

} // namespace

FdoNotifier::FdoNotifier(QObject *parent) : Notifier(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_resolved = true; // headless or no session bus: stay a no-op
        return;
    }

    // By string, because QDBusConnection::connect has no function-pointer
    // overload; these two are why the handlers are slots.
    bus.connect(kService, kPath, kIface, QStringLiteral("ActionInvoked"), this,
                SLOT(onActionInvoked(uint, QString)));
    bus.connect(kService, kPath, kIface, QStringLiteral("NotificationClosed"),
                this, SLOT(onNotificationClosed(uint, uint)));

    queryCapabilities();
}

void FdoNotifier::queryCapabilities() {
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, kIface, QStringLiteral("GetCapabilities"));
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *w) {
                QDBusPendingReply<QStringList> reply = *w;
                w->deleteLater();

                m_resolved = true;
                if (reply.isError()) {
                    m_held.clear(); // no daemon; the alerts are moot
                    return;
                }

                const QStringList caps = reply.value();
                m_bodyMarkup = caps.contains(QLatin1String("body-markup"));
                m_actions = caps.contains(QLatin1String("actions"));
                m_available = true;

                const QHash<QString, Alert> held = m_held;
                m_held.clear();
                for (auto it = held.constBegin(); it != held.constEnd(); ++it)
                    show(it.key(), it.value());
            });
}

void FdoNotifier::show(const QString &key, const Alert &alert) {
    if (!m_resolved) {
        m_held.insert(key, alert); // newest wins, same as replacing on screen
        return;
    }
    if (!m_available)
        return;

    Entry &entry = m_entries[key];
    if (entry.inFlight) {
        // A catch-up burst for one chat can outrun the bus, and a Notify
        // carrying replaces_id=0 stacks a second popup instead of rewriting
        // the first. So the newest alert waits for the id to come back.
        entry.hasQueued = true;
        entry.queued = alert;
        entry.retractQueued = false;
        return;
    }

    entry.inFlight = true;
    sendNotify(key, alert, entry.id);
}

void FdoNotifier::retract(const QString &key) {
    m_held.remove(key);

    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;

    if (it->inFlight) {
        it->hasQueued = false;
        it->retractQueued = true; // closed as soon as it has an id
        return;
    }

    if (it->id) {
        sendClose(it->id);
        m_byId.remove(it->id);
    }
    m_entries.erase(it);
}

void FdoNotifier::retractAll() {
    m_held.clear();
    const QStringList keys(m_entries.keyBegin(), m_entries.keyEnd());
    for (const QString &key : keys)
        retract(key);
}

void FdoNotifier::sendNotify(const QString &key, const Alert &alert,
                             uint replaces) {
    QVariantMap hints;
    // Standard IM category, so a daemon can apply whatever rule the user has
    // for chat messages.
    hints.insert(QStringLiteral("category"), QStringLiteral("im.received"));
    // Points at our .desktop entry, which is where the icon and the app's
    // notification settings come from.
    if (!QCoreApplication::applicationName().isEmpty())
        hints.insert(QStringLiteral("desktop-entry"),
                     QCoreApplication::applicationName());
    // No urgency hint, mention or not: whether a mention outranks
    // do-not-disturb is the user's setting to make, not ours.

    QStringList actions;
    if (m_actions)
        actions << kDefaultAction << tr("Open");

    // Only escaped where the daemon actually parses markup; escaping for one
    // that does not would show the entities to the user.
    const QString body =
        m_bodyMarkup ? alert.body.toHtmlEscaped() : alert.body;

    // "Ann (12)", as Android's Notifications.java builds it. `unread` is the
    // chat's whole count, so a burst reads as one alert that grew.
    const QString summary = alert.unread > 1
                                ? tr("%1 (%2)").arg(alert.nick).arg(alert.unread)
                                : alert.nick;

    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, kIface, QStringLiteral("Notify"));
    call.setArguments({
        QStringLiteral("Quack"), // app_name
        replaces,
        QString(), // app_icon: the desktop-entry hint carries it
        summary,
        body,
        actions,
        hints,
        -1, // expire_timeout: whatever the desktop's own default is
    });

    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, key](QDBusPendingCallWatcher *w) {
                QDBusPendingReply<uint> reply = *w;
                w->deleteLater();
                onNotifyReply(key, reply.isError() ? 0u : reply.value(),
                              !reply.isError());
            });
}

void FdoNotifier::sendClose(uint id) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kPath, kIface, QStringLiteral("CloseNotification"));
    call.setArguments({id});
    QDBusConnection::sessionBus().asyncCall(call);
}

void FdoNotifier::onNotifyReply(const QString &key, uint id, bool ok) {
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;

    it->inFlight = false;
    if (ok && id) {
        if (it->id && it->id != id)
            m_byId.remove(it->id);
        it->id = id;
        m_byId.insert(id, key);
    }
    settle(key, *it);
}

void FdoNotifier::settle(const QString &key, Entry &entry) {
    if (entry.retractQueued) {
        entry.retractQueued = false;
        retract(key); // drops the entry, so nothing below may touch it
        return;
    }

    if (entry.hasQueued) {
        entry.hasQueued = false;
        const Alert next = entry.queued;
        entry.inFlight = true;
        sendNotify(key, next, entry.id);
        return;
    }

    // Nothing on screen and nothing pending: the Notify failed outright.
    if (!entry.id)
        m_entries.remove(key);
}

void FdoNotifier::onActionInvoked(uint id, const QString &action) {
    if (action != kDefaultAction)
        return;
    const QString key = m_byId.value(id);
    if (key.isEmpty())
        return; // another app's notification; the signal is broadcast
    emit activated(key);
}

void FdoNotifier::onNotificationClosed(uint id, uint reason) {
    Q_UNUSED(reason) // dismissed, expired or closed by us: all the same here

    const QString key = m_byId.take(id);
    if (key.isEmpty())
        return;

    auto it = m_entries.find(key);
    if (it == m_entries.end() || it->id != id)
        return; // already superseded by a newer notification for this chat

    it->id = 0;
    if (!it->inFlight && !it->hasQueued && !it->retractQueued)
        m_entries.erase(it);
}

Notifier *createPlatformNotifier(QObject *parent) {
    return new FdoNotifier(parent);
}
