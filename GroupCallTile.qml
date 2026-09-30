import QtQuick
import QtQuick.Controls
import QtMultimedia
import Quack

// One person in a group call: their video when it flows, their avatar when it
// does not, and a name pill in the corner. The window lays these out in a grid
// and hands each one a participant row, or itself for the self-view.
//
// Drawn on the call's own dark stage rather than the theme.
Rectangle {
    id: tile

    property string account: ""
    property string name: ""
    // Bare JID for the avatar; a room that hides it leaves this empty and the
    // initial comes off the nick instead.
    property string jid: ""
    // expected | connecting | active | ended | failed, or "self". Not `state`:
    // that is Item's, and the States machinery reads it.
    property string legState: "connecting"
    property string reason: ""
    property string warning: ""
    property bool hasVideo: false
    property var channel: ({})
    property bool self: false
    // The self-view is mirrored, so it moves the way you do.
    property bool mirrored: self
    property bool micMuted: false
    property bool cameraOff: false

    readonly property bool gone: state === "ended" || state === "failed"
    readonly property bool waiting: state === "expected" || state === "connecting"

    readonly property string statusText: {
        switch (tile.legState) {
        case "expected":   return qsTr("Waiting to connect…")
        case "connecting": return qsTr("Connecting…")
        case "ended":      return tile.reason !== "" ? tile.reason : qsTr("Left")
        case "failed":     return tile.reason !== "" ? qsTr("Failed: %1").arg(tile.reason)
                                                     : qsTr("Failed")
        default:           return tile.warning
        }
    }

    radius: 14
    color: "#1c1f24"
    border.width: tile.legState === "active" ? 1 : 0
    border.color: "#3a4048"
    clip: true
    antialiasing: true

    Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    Behavior on height { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

    VideoOutput {
        id: out
        anchors.fill: parent
        visible: tile.hasVideo && !tile.cameraOff
        fillMode: VideoOutput.PreserveAspectCrop
        transform: Scale {
            origin.x: out.width / 2
            xScale: tile.mirrored ? -1 : 1
        }
        VideoSurface {
            videoSink: out.videoSink
            channel: tile.channel || ({})
        }
    }

    // The avatar when there is no picture, sized to the tile.
    Column {
        anchors.centerIn: parent
        spacing: 10
        visible: !out.visible
        opacity: tile.gone ? 0.45 : 1

        Avatar {
            id: face
            anchors.horizontalCenter: parent.horizontalCenter
            readonly property int side: Math.round(Math.max(40, Math.min(tile.width, tile.height) * 0.36))
            width: side
            height: side
            account: tile.account
            jid: tile.jid
            label: tile.name
            initialsPixelSize: Math.round(side * 0.4)
        }

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6
            visible: tile.statusText !== "" && !tile.self

            BusyIndicator {
                anchors.verticalCenter: parent.verticalCenter
                running: tile.waiting
                visible: tile.waiting
                width: 16; height: 16
                palette.dark: "#c9ced6"; palette.text: "#c9ced6"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: tile.statusText
                color: tile.legState === "failed" ? Theme.negative
                     : tile.gone ? "#8a919c" : "#c9ced6"
                font.pixelSize: 12
                elide: Text.ElideRight
                width: Math.min(implicitWidth, tile.width - 40)
            }
        }
    }

    // A dimmed wash over someone whose leg is gone, so the wall says who is
    // still here at a glance.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: tile.gone ? 0.35 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // The name, and whether they are muted.
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: pill.implicitWidth + 20
        height: 26
        radius: 13
        color: "#000000"
        opacity: 0.55
    }
    Row {
        id: pill
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 8
        anchors.leftMargin: 18
        height: 26
        spacing: 6
        Glyph {
            anchors.verticalCenter: parent.verticalCenter
            visible: tile.micMuted
            path: Icons.micOff
            color: Theme.negative
            size: 14
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: tile.self ? qsTr("You") : tile.name
            color: "#ffffff"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
            width: Math.min(implicitWidth, tile.width - 60)
        }
    }

    // A camera they announced but that is not showing yet, or ours switched
    // off: a small mark in the other corner rather than a second status line.
    Glyph {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 10
        visible: tile.cameraOff
        path: Icons.videoCamOff
        color: "#8a919c"
        size: 16
    }
}
