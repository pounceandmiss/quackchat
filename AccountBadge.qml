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
    // False while the model is still waiting to be told: the dot goes hollow
    // rather than claiming a state the backend has not sent yet.
    property bool statusKnown: true
    property bool current: false
    property real dotSize: 13
    // Punches the dot out of the fill it sits on: the rail's in the drawer, the
    // header's surface in the list.
    property color ringColor: Theme.rail

    implicitWidth: 44
    implicitHeight: 44

    readonly property color stateColor: {
        if (!badge.statusKnown || !badge.acctEnabled)
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
        objectName: "accountBadgeAvatar"
        anchors.fill: parent
        radius: badge.current ? width / 4 : width / 2
        // Dimming is what says "disabled", so it waits until that is settled.
        opacity: badge.statusKnown && !badge.acctEnabled ? 0.45 : 1.0
        Behavior on radius { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        initialsPixelSize: Math.round(height * 0.4)
        account: badge.jid
        jid: badge.jid
        label: badge.jid
    }

    // The ring that punches the dot out of the fill, with the state inside it:
    // filled once the state is in, an empty outline until then.
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: badge.dotSize; height: badge.dotSize
        radius: width / 2
        color: badge.ringColor

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: width / 2
            color: badge.statusKnown ? badge.stateColor : "transparent"
            border.width: badge.statusKnown ? 0 : 1.5
            border.color: badge.stateColor
        }
    }
}
