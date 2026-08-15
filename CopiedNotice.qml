import QtQuick
import Quack

// The pill that says something reached the clipboard, since a copy is silent
// otherwise. Anchored by whoever puts one up; show() is what raises it, and it
// takes itself away again.
Rectangle {
    id: notice
    property alias text: noticeText.text

    width: noticeText.implicitWidth + 28
    height: 34
    radius: 17
    color: Theme.textPrimary
    opacity: 0
    visible: opacity > 0

    function show() {
        notice.opacity = 0.92
        hideTimer.restart()
    }

    Behavior on opacity { NumberAnimation { duration: 180 } }
    Timer {
        id: hideTimer
        interval: 1400
        onTriggered: notice.opacity = 0
    }

    Text {
        id: noticeText
        anchors.centerIn: parent
        color: Theme.surface
        font.pixelSize: 12
    }
}
