pragma ComponentBehavior: Bound

import QtQuick
import Quack

// What the app has to say about itself: the backend is gone, never came up, or
// reported a failure with nothing to answer. The backend is shared by every
// window, so whichever one you are looking at is the one that has to say so.
// Floats over the page rather than taking a strip of it, since two of the three
// states are momentary.
Rectangle {
    id: notice
    objectName: "backendNotice"
    z: 10

    // A failure reported out of the blue (error <Background>): advisory, so it
    // takes itself away again.
    property string reported: ""
    // A backend that is gone is not advisory, and stays up until it is back.
    readonly property bool down: App.backend.attached && !App.backend.running
    readonly property string message: {
        if (App.backendStartFailed)
            return qsTr("The backend could not start. Quack has to be restarted.")
        if (notice.down)
            return qsTr("Reconnecting to the backend.")
        return notice.reported
    }

    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    // Clear of a notch or status bar; zero on desktop.
    anchors.topMargin: 10 + SafeArea.margins.top
    width: Math.min(parent.width - 24, 420)
    height: messageText.implicitHeight + 20
    radius: 8
    color: Theme.surface
    border.width: 1
    // Reconnecting is a wait; the other two are failures.
    border.color: notice.down && !App.backendStartFailed ? Theme.warning
                                                         : Theme.negative
    visible: notice.message !== ""

    Text {
        id: messageText
        anchors.centerIn: parent
        width: parent.width - 24
        text: notice.message
        color: Theme.textPrimary
        font.pixelSize: 13
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Connections {
        target: App
        function onBackendError(message) { notice.reported = message }
    }

    // Only the advisory one can be dismissed: the other two are still true
    // after a tap.
    TapHandler { onTapped: notice.reported = "" }
    Timer {
        running: notice.reported !== ""
        interval: 6000
        onTriggered: notice.reported = ""
    }
}
