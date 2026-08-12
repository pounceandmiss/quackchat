// The factory for platforms with no Notifier - Windows, macOS, and a Linux
// build configured without Qt6::DBus. A null notifier disables alerts and
// nothing else.
//
// Android is null here on purpose rather than for want of an implementation:
// it posts its own from the :backend service (Notifications.java), the process
// holding the connection, because Qt kills the UI process on activity destroy.
#include "Notifier.h"

Notifier *createPlatformNotifier(QObject *parent) {
    Q_UNUSED(parent)
    return nullptr;
}
