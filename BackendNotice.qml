pragma ComponentBehavior: Bound

import QtQuick
import Quack

// What the app has to say about its backend, put on a window's notice stack:
// gone, never came up, or something in it broke with no request behind it.
// The backend is shared by every window, so whichever one you are looking at
// is the one that has to say so. Draws nothing itself.
Item {
    id: notice
    objectName: "backendNotice"

    property NoticeHost host: null

    // A backend that is gone stays on the stack until it is back.
    readonly property bool down: App.backend.attached && !App.backend.running
    readonly property string message: {
        if (App.backendStartFailed)
            return App.backendStartError !== ""
                ? qsTr("The backend could not start: %1").arg(App.backendStartError)
                : qsTr("The backend could not start. Quack has to be restarted.")
        if (notice.down)
            return qsTr("Reconnecting to the backend.")
        return ""
    }
    // The reason is worth a bug report; "reconnecting" isn't.
    readonly property bool copyable: App.backendStartFailed && App.backendStartError !== ""

    // A backend away for less than this is not worth a card: on Android the UI
    // reattaches to the service on every launch, and a card each time would
    // only flash. A start that failed has nothing to wait for.
    property bool settled: false
    readonly property bool shown: notice.message !== ""
                                  && (App.backendStartFailed || notice.settled)
    Timer {
        running: notice.down && !App.backendStartFailed && !notice.settled
        interval: 1500
        onTriggered: notice.settled = true
    }
    onDownChanged: if (!notice.down) notice.settled = false

    function sync() {
        if (!notice.host)
            return
        if (notice.shown)
            notice.host.show("backend", {
                text: notice.message,
                tone: App.backendStartFailed ? "negative" : "warning",
                copyable: notice.copyable,
                copyText: App.backendStartError
            })
        else
            notice.host.hide("backend")
    }
    // Each of these on its own: they hang off the same App signals, in no
    // order a sync on one could count on the others having caught up.
    onShownChanged: notice.sync()
    onMessageChanged: notice.sync()
    onCopyableChanged: notice.sync()
    onHostChanged: notice.sync()
    Component.onCompleted: notice.sync()

    Connections {
        target: App
        // error <Background>: something broke that no request owns. The
        // backend has logged it with its trace; this is so it doesn't go
        // unnoticed. One card per message, which a repeat refreshes, and
        // "Don't show again" for one that won't stop.
        function onBackendError(message) {
            if (!notice.host)
                return
            notice.host.post({
                key: "bg:" + message,
                text: qsTr("Something went wrong in the background: %1").arg(message),
                copyText: message,
                tone: "negative",
                copyable: true,
                actionText: qsTr("Don't show again")
            }, () => App.muteBackendError(message))
        }
        // Muted from any window: off every window's stack.
        function onBackendErrorMuted(message) {
            if (notice.host)
                notice.host.dismiss("bg:" + message)
        }
    }
}
