// Notifier over org.freedesktop.Notifications, i.e. every Linux desktop -
// Plasma, GNOME, XFCE and the standalone daemons all speak it. Linux-only, and
// only built when Qt6::DBus was found.
//
// Every call to the bus is async: a wedged notification daemon must not stall
// the GUI thread, which is also why this does not use QDBusInterface (its
// constructor introspects synchronously).
#ifndef FDONOTIFIER_H
#define FDONOTIFIER_H

#include <QHash>
#include <QString>

#include "Notifier.h"

class FdoNotifier : public Notifier {
    Q_OBJECT

public:
    explicit FdoNotifier(QObject *parent = nullptr);

    // False until the daemon has answered GetCapabilities, and forever if
    // there is none (or no session bus). Alerts raised before then are held
    // and flushed on the answer, so nothing is lost to the startup race and
    // nothing is sent before we know how the body will be parsed.
    bool isAvailable() const { return m_available; }

    void show(const QString &key, const Alert &alert) override;
    void retract(const QString &key) override;
    void retractAll() override;

private slots:
    // Named for the bus signals they are connected to by string.
    void onActionInvoked(uint id, const QString &action);
    void onNotificationClosed(uint id, uint reason);

private:
    // What one chat's alert is doing. A key with no entry has nothing on
    // screen and nothing in flight.
    struct Entry {
        uint id = 0;            // live daemon id, 0 while none
        bool inFlight = false;  // a Notify is out, its id not back yet
        bool hasQueued = false; // ... and this arrived meanwhile
        Alert queued;
        bool retractQueued = false; // ... or a retract did
    };

    void queryCapabilities();
    void sendNotify(const QString &key, const Alert &alert, uint replaces);
    void sendClose(uint id);
    void onNotifyReply(const QString &key, uint id, bool ok);
    // Applies whatever piled up behind an in-flight Notify. Can drop `entry`
    // from the table, so callers must not touch it afterwards.
    void settle(const QString &key, Entry &entry);

    bool m_available = false;
    bool m_resolved = false; // capabilities came back, one way or the other
    // Daemons only render markup in the body when they advertise it, and then
    // an unescaped "<" from a message eats the rest of the line.
    bool m_bodyMarkup = false;
    bool m_actions = false;

    QHash<QString, Entry> m_entries;
    QHash<uint, QString> m_byId; // live id -> key, for the bus signals
    // Held until capabilities resolve; one alert per key, newest wins.
    QHash<QString, Alert> m_held;
};

#endif // FDONOTIFIER_H
