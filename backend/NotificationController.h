// Desktop alerts: tacky's notify events in, this platform's Notifier out.
// Exposed to QML as `App.notifications` only for activated() - what "open this
// chat" means belongs to the window layer, not here.
//
// No policy lives here, deliberately. Which message is worth interrupting the
// user over, when the alert is cancelled, how an offline catch-up is capped
// and what mute/mentions do are all decided in tacky against the read
// watermark, the same events Android's Notifications.java already runs on.
// This only translates them.
#ifndef NOTIFICATIONCONTROLLER_H
#define NOTIFICATIONCONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class Notifier;
class TackyBackend;

class NotificationController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit NotificationController(QObject *parent = nullptr);

    void setBackend(TackyBackend *backend);

    // Takes ownership, replacing any previous notifier; nullptr disables
    // alerts, which is what a platform with no implementation gets. Called
    // from startFromEnvironment() and never from QML, so loading the UI in a
    // test touches no session bus.
    void setNotifier(Notifier *notifier);
    Notifier *notifier() const { return m_notifier; }

    // Public so a test can feed it canned tacky JSON, the same way the
    // ChatListModel tests do; ordinarily the backend's event() drives it.
    void handleEvent(const QString &module, const QString &name,
                     const QVariant &args);

signals:
    // The user picked a chat's alert. Nothing has been marked read by then -
    // showing the chat is what moves the watermark, and that is what retracts
    // whatever else is on screen for it.
    void activated(const QString &acc, const QString &jid);

private:
    void onRunningChanged();

    TackyBackend *m_backend = nullptr;
    Notifier *m_notifier = nullptr; // owned
};

#endif // NOTIFICATIONCONTROLLER_H
