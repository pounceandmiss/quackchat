import QtQuick
import Quack

// An account's avatar with its connection state as a dot in the corner. The
// rail draws one per account, the conversations header one for the account it
// is showing.
Item {
    id: badge
    property string jid: ""
    property string connState: ""
    property bool acctEnabled: true
    property bool current: false
    property real dotSize: 13
    // Punches the dot out of the fill it sits on: the rail's in the drawer, the
    // header's surface in the list.
    property color ringColor: Theme.rail

    implicitWidth: 44
    implicitHeight: 44

    readonly property color stateColor: {
        if (!badge.acctEnabled)
            return Theme.textDim
        switch (badge.connState) {
        case "connected": return Theme.positive
        case "auth-error":
        case "conn-error": return Theme.negative
        case "waiting":
        case "disconnected": return Theme.warning
        case "": return Theme.textDim
        default: return Theme.accent2 // connecting / authenticating / binding
        }
    }

    // Circle that squares off into a rounded tile while current.
    Avatar {
        anchors.fill: parent
        radius: badge.current ? width / 4 : width / 2
        opacity: badge.acctEnabled ? 1.0 : 0.45
        Behavior on radius { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        initialsPixelSize: Math.round(height * 0.4)
        account: badge.jid
        jid: badge.jid
        label: badge.jid
    }

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: badge.dotSize; height: badge.dotSize
        radius: width / 2
        color: badge.stateColor
        border.width: 2
        border.color: badge.ringColor
    }
}
