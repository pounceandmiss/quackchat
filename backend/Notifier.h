// How the app puts an alert in front of the user, per desktop. Linux talks to
// org.freedesktop.Notifications over D-Bus (FdoNotifier); everywhere else gets
// a null factory and no alerts. QtCore only at this level.
//
// The three verbs below are what tacky's notify module asks for: one alert per
// chat, rewritten in place by the next message rather than stacked on, and
// taken down when the read watermark moves.
#ifndef NOTIFIER_H
#define NOTIFIER_H

#include <QObject>
#include <QString>

class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr) : QObject(parent) {}

    // What one chat's alert says. `unread` is the chat's whole unread count,
    // not the size of this burst - tacky sends the true total even when it
    // caps how many alerts one catch-up emits.
    struct Alert {
        QString nick; // who to show as the sender
        QString body; // plain text, never markup
        int unread = 0;
        bool mention = false;
    };

    // Shows `alert`, replacing whatever is already on screen under `key`.
    // Callers key by chat, so a second message rewrites the first.
    virtual void show(const QString &key, const Alert &alert) = 0;

    // Takes down `key`'s alert if it is still up; no-op otherwise.
    virtual void retract(const QString &key) = 0;

    // Every alert at once, for when the backend goes away and what is on
    // screen can no longer be trusted to still be unread.
    virtual void retractAll() = 0;

signals:
    // The user picked the alert itself (not a button on it), i.e. "take me to
    // this chat".
    void activated(const QString &key);
};

// The implementation for this platform, or nullptr where there is none. Owned
// by the caller via `parent`.
Notifier *createPlatformNotifier(QObject *parent = nullptr);

#endif // NOTIFIER_H
