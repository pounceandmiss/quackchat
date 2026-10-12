pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quack

// Android's way back to a call: a strip across the top per call we are in,
// opening its native screen. Never shown on desktop, which has call windows.
ColumnLayout {
    id: bar

    readonly property bool enabledHere: Qt.platform.os === "android"

    spacing: 0
    visible: bar.enabledHere

    property real now: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: bar.height > 0
        triggeredOnStart: true
        onTriggered: bar.now = Date.now()
    }
    function clock(ms) {
        const total = Math.max(0, Math.floor(ms / 1000))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const mm = (h > 0 && m < 10 ? "0" : "") + m
        const ss = (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }
    // What the chat list calls a chat, else its address.
    function chatName(account, jid) {
        const list = App.chatListFor(account)
        const name = list ? (list.entryFor(jid).name ?? "") : ""
        return name !== "" ? name : jid
    }

    component Strip: Rectangle {
        id: strip
        property string title
        property string detail
        signal open

        Layout.fillWidth: true
        implicitHeight: 40
        color: Theme.positive

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10
            Glyph {
                path: Icons.call
                color: "#ffffff"
                size: 18
            }
            Text {
                Layout.fillWidth: true
                text: strip.title
                color: "#ffffff"
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                text: strip.detail
                color: "#ffffff"
                font.pixelSize: 13
                font.features: { "tnum": 1 }
            }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: strip.open()
        }
    }

    Repeater {
        id: calls
        model: bar.enabledHere ? App.calls : null
        delegate: Strip {
            required property string sid
            required property string account
            required property string peer
            required property string state
            required property bool terminal
            required property var activeSince

            visible: !terminal
            title: bar.chatName(account, peer)
            detail: {
                switch (state) {
                case "active":
                    return activeSince > 0 ? bar.clock(bar.now - activeSince / 1000)
                                           : qsTr("Connected")
                case "incoming":   return qsTr("Incoming call")
                case "ringing":    return qsTr("Ringing…")
                case "connecting": return qsTr("Connecting…")
                default:           return qsTr("Calling…")
                }
            }
            onOpen: App.showCallScreen(account, sid)
        }
    }

    Repeater {
        id: groups
        model: bar.enabledHere ? App.groupCalls : null
        delegate: Strip {
            required property GroupCall call

            visible: call.inCall
            title: bar.chatName(call.account, call.jid)
            detail: call.phase === "live" && call.startedAt > 0
                    ? qsTr("%n in call", "", call.count) + "  ·  "
                      + bar.clock(bar.now - call.startedAt)
                    : qsTr("Joining…")
            onOpen: App.showGroupCallScreen(call.account, call.callJid)
        }
    }
}
