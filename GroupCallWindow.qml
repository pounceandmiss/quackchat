pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// A room's call: a wall of tiles, one per participant and one for us, over a
// bar of controls. AppWindows builds one per GroupCallsModel row and shows it
// while the row's phase is not idle, so this decides nothing about the call -
// it reads `call` and calls back into it.
//
// Its own dark stage, whatever the theme: video reads best on near-black.
ApplicationWindow {
    id: win
    objectName: "groupCallWindow"

    required property GroupCall call

    readonly property string room: win.call ? win.call.jid : ""
    readonly property string phase: win.call ? win.call.phase : "idle"
    readonly property bool live: win.phase === "live"
    readonly property bool ended: win.phase === "ended"
    readonly property int others: win.call ? win.call.participants.count : 0

    // The stage's own palette.
    readonly property color stage: "#101214"
    readonly property color bar: "#181b1f"
    readonly property color pill: "#2a2e35"
    readonly property color pillHover: "#363b43"
    readonly property color ink: "#f2f4f7"
    readonly property color inkDim: "#9aa3ae"

    width: Theme.mobile ? Screen.width : 900
    height: Theme.mobile ? Screen.height : 620
    minimumWidth: Theme.mobile ? 0 : 520
    minimumHeight: Theme.mobile ? 0 : 400
    title: qsTr("Group call — %1").arg(win.room)
    color: win.stage

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }
    Shortcut { sequence: "Escape"; onActivated: win.leave() }

    // The one way out: leave if we are in, let go of the row if we are not.
    // The row's phase going idle is what actually takes this window down.
    function leave() {
        if (!win.call)
            return
        if (win.call.inCall)
            win.call.leave()
        else
            win.call.dismiss()
    }

    onClosing: win.leave()

    // A call we walked out of has nothing left to say; one that ended on us
    // stays up with the reason and "Rejoin". The second clause is a call that
    // ended while nobody was looking - a join refused before the window was
    // ever shown - which would otherwise sit in the model forever.
    Timer {
        running: win.ended && win.call && (win.call.reason === "" || !win.visible)
        interval: 900
        onTriggered: win.call.dismiss()
    }

    // The clock in the header. Ticks only while there is a call to time.
    property real now: Date.now()
    Timer {
        running: win.live && win.visible
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: win.now = Date.now()
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

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        spacing: 0

        // Header: the room, who is here, and how long.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 56

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4
                    color: win.live ? Theme.positive : win.inkDim
                    SequentialAnimation on opacity {
                        running: win.phase === "joining"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: win.room
                    color: win.ink
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                Text {
                    objectName: "groupCallClock"
                    visible: win.live
                    text: win.clock(win.now - (win.call ? win.call.startedAt : 0))
                    color: win.inkDim
                    font.pixelSize: 14
                    font.family: "monospace"
                }
                Rectangle {
                    Layout.preferredWidth: peopleRow.implicitWidth + 20
                    Layout.preferredHeight: 28
                    radius: 14
                    color: win.pill
                    Row {
                        id: peopleRow
                        anchors.centerIn: parent
                        spacing: 6
                        Glyph {
                            anchors.verticalCenter: parent.verticalCenter
                            path: Icons.group
                            color: win.inkDim
                            size: 16
                        }
                        Text {
                            objectName: "groupCallCount"
                            anchors.verticalCenter: parent.verticalCenter
                            // Everyone announcing the call, us included once
                            // we are in; until then what the room says.
                            text: win.call ? Math.max(win.call.count,
                                                      win.others + (win.live ? 1 : 0)) : 0
                            color: win.ink
                            font.pixelSize: 13
                            font.bold: true
                        }
                    }
                }
            }
        }

        // <Warning> is informational: the call goes on around it.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 8
            visible: win.call && win.call.warning !== ""
            Layout.preferredHeight: warnText.implicitHeight + 14
            radius: 8
            color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.16)
            Text {
                id: warnText
                anchors.fill: parent
                anchors.margins: 7
                anchors.leftMargin: 12
                text: win.call ? win.call.warning : ""
                color: Theme.warning
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }

        // The wall. Tiles are placed by hand rather than by a GridLayout: the
        // number of columns is whatever gives the biggest tiles at 16:9, and a
        // short last row is centred, which no layout does on its own.
        Item {
            id: wall
            objectName: "groupCallWall"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12

            readonly property int gap: 10
            readonly property int n: win.others + 1
            readonly property real aspect: 16 / 9

            // The widest a 16:9 tile can be with `c` columns in this much room:
            // limited by the width or by the height of the rows it makes. The
            // height follows from the width.
            function fitWidth(c: int): real {
                const rows = Math.ceil(wall.n / c)
                const w = (wall.width - (c - 1) * wall.gap) / c
                const h = (wall.height - (rows - 1) * wall.gap) / rows
                return Math.min(w, h * wall.aspect)
            }

            // Whichever column count gives the biggest tiles.
            readonly property int cols: {
                let best = 1
                let bestW = 0
                for (let c = 1; c <= wall.n; ++c) {
                    const w = wall.fitWidth(c)
                    if (w > bestW) {
                        best = c
                        bestW = w
                    }
                }
                return best
            }
            readonly property int rows: Math.ceil(wall.n / wall.cols)
            readonly property real tileW: Math.floor(wall.fitWidth(wall.cols))
            readonly property real tileH: Math.floor(wall.fitWidth(wall.cols) / wall.aspect)
            readonly property real firstRowY: (wall.height - (wall.rows * wall.tileH + (wall.rows - 1) * wall.gap)) / 2

            function tileX(i) {
                const row = Math.floor(i / wall.cols)
                const inRow = row === wall.rows - 1 ? wall.n - row * wall.cols : wall.cols
                const rowWidth = inRow * wall.tileW + (inRow - 1) * wall.gap
                return (wall.width - rowWidth) / 2 + (i % wall.cols) * (wall.tileW + wall.gap)
            }
            function tileY(i) {
                return wall.firstRowY + Math.floor(i / wall.cols) * (wall.tileH + wall.gap)
            }

            Repeater {
                model: win.call ? win.call.participants : null
                delegate: GroupCallTile {
                    id: peerTile
                    objectName: "groupCallTile"
                    required property int index
                    required property string nick
                    required property bool video
                    required property var remoteVideo
                    // The tile's own properties, filled straight off the row.
                    required jid
                    required legState
                    required reason
                    required warning
                    required hasVideo

                    x: wall.tileX(peerTile.index)
                    y: wall.tileY(peerTile.index)
                    width: wall.tileW
                    height: wall.tileH
                    account: win.call.account
                    name: peerTile.nick
                    channel: peerTile.remoteVideo
                    cameraOff: peerTile.video && !peerTile.hasVideo && peerTile.legState === "active"
                }
            }

            GroupCallTile {
                objectName: "groupCallSelfTile"
                x: wall.tileX(wall.n - 1)
                y: wall.tileY(wall.n - 1)
                width: wall.tileW
                height: wall.tileH
                self: true
                account: win.call ? win.call.account : ""
                jid: win.call ? win.call.account : ""
                name: qsTr("You")
                legState: "self"
                hasVideo: win.call ? win.call.sendingVideo : false
                channel: win.call ? win.call.preview : ({})
                micMuted: App.audio.captureMuted
                cameraOff: win.call ? (win.call.video && !win.call.cameraOn) : false
            }

            // Alone, or not in yet: say so over the wall rather than leave a
            // single tile to explain itself.
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 18
                visible: !win.ended && win.others === 0
                width: hintRow.implicitWidth + 28
                height: 40
                radius: 20
                color: win.bar
                border.width: 1
                border.color: "#2a2e35"

                Row {
                    id: hintRow
                    anchors.centerIn: parent
                    spacing: 10
                    BusyIndicator {
                        anchors.verticalCenter: parent.verticalCenter
                        running: win.phase === "joining"
                        visible: running
                        width: 16; height: 16
                        palette.dark: win.inkDim; palette.text: win.inkDim
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: win.phase === "joining" ? qsTr("Joining the call…")
                                                      : qsTr("Nobody else is here yet")
                        color: win.ink
                        font.pixelSize: 13
                    }
                    AbstractButton {
                        id: inviteHint
                        anchors.verticalCenter: parent.verticalCenter
                        visible: win.live
                        text: qsTr("Invite")
                        implicitWidth: inviteLabel.implicitWidth + 20
                        implicitHeight: 26
                        contentItem: Text {
                            id: inviteLabel
                            text: inviteHint.text
                            color: win.ink
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 13
                            color: inviteHint.hovered ? Theme.accentDeep : Theme.accent
                            opacity: inviteHint.pressed ? 0.8 : 1
                        }
                        onClicked: invitePopup.open()
                    }
                }
            }
        }

        // Controls: what you send, who is here, and the way out on the right.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            color: win.bar

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.mobile ? 12 : 22

                CallButton {
                    objectName: "groupCallMic"
                    iconPath: App.audio.captureMuted ? Icons.micOff : Icons.mic
                    fill: App.audio.captureMuted ? Theme.negative : win.pill
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    diameter: 48
                    text: App.audio.captureMuted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: App.audio.setCaptureMuted(!App.audio.captureMuted)
                }
                CallButton {
                    iconPath: App.audio.playbackMuted ? Icons.volumeOff : Icons.volumeUp
                    fill: App.audio.playbackMuted ? Theme.negative : win.pill
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    diameter: 48
                    text: qsTr("Speaker")
                    onClicked: App.audio.setPlaybackMuted(!App.audio.playbackMuted)
                }
                // Only a call that offered a camera has one to mute: setVideo
                // does not add video to legs that negotiated without it.
                CallButton {
                    objectName: "groupCallCamera"
                    visible: win.call ? win.call.video : false
                    iconPath: win.call && win.call.cameraOn ? Icons.videoCam : Icons.videoCamOff
                    fill: win.call && win.call.cameraOn ? win.pill : Theme.negative
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    diameter: 48
                    text: win.call && win.call.cameraOn ? qsTr("Stop video") : qsTr("Start video")
                    onClicked: win.call.setVideo(!win.call.cameraOn)
                }
                CallButton {
                    objectName: "groupCallInvite"
                    iconPath: Icons.personAdd
                    fill: win.pill
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    diameter: 48
                    enabled: win.live
                    opacity: enabled ? 1 : 0.5
                    text: qsTr("Invite")
                    onClicked: invitePopup.open()
                }

                Item { Layout.preferredWidth: Theme.mobile ? 4 : 16 }

                CallButton {
                    objectName: "groupCallLeave"
                    iconPath: win.ended ? Icons.close : Icons.callEnd
                    fill: win.ended ? win.pill : Theme.negative
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    diameter: 48
                    text: win.ended ? qsTr("Close") : qsTr("Leave")
                    onClicked: win.leave()
                }
            }
        }
    }

    // How it ended, over the wall it ended on. "Rejoin" is a fresh join: the
    // row is the same, the call is not. Modal, so the keys and the controls
    // underneath wait for an answer as the mouse does.
    Popup {
        id: endedOverlay
        objectName: "groupCallEnded"
        parent: Overlay.overlay
        x: 0
        y: parent ? parent.SafeArea.margins.top : 0
        width: parent ? parent.width : 0
        height: parent ? parent.height - y : 0
        padding: 0
        modal: true
        closePolicy: Popup.NoAutoClose
        visible: win.ended
        background: Rectangle { color: Qt.rgba(0, 0, 0, 0.72) }
        Overlay.modal: Item {}

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 6
            width: Math.min(endedOverlay.width - 48, 420)

            Text {
                Layout.fillWidth: true
                text: qsTr("Call ended")
                color: win.ink
                font.pixelSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                Layout.fillWidth: true
                text: win.call ? win.call.reason : ""
                visible: text !== ""
                color: win.inkDim
                font.pixelSize: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 22
                spacing: 20
                CallButton {
                    objectName: "groupCallRejoin"
                    iconPath: Icons.call
                    fill: Theme.positive
                    glyphColor: "#ffffff"
                    captionColor: win.inkDim
                    text: qsTr("Rejoin")
                    onClicked: win.call.join(win.call.video)
                }
                CallButton {
                    iconPath: Icons.close
                    fill: win.pill
                    glyphColor: win.ink
                    captionColor: win.inkDim
                    text: qsTr("Close")
                    onClicked: win.call.dismiss()
                }
            }
        }
    }

    // XEP-0482: an invite to this call, sent to a JID. It is a note, not a
    // ring; a hosted call's room lets them in.
    Popup {
        id: invitePopup
        objectName: "groupCallInvitePopup"
        anchors.centerIn: parent
        width: Math.min(win.width - 40, 380)
        modal: true
        focus: true
        padding: 20
        onOpened: { sentTo = ""; inviteField.forceActiveFocus() }
        onClosed: inviteField.text = ""

        property string sentTo: ""

        background: Rectangle {
            radius: 14
            color: win.bar
            border.width: 1
            border.color: "#2a2e35"
        }

        function send() {
            const jid = Jid.bare(inviteField.text)
            if (!Jid.plausible(jid))
                return
            win.call.invite(jid)
            invitePopup.sentTo = jid
            inviteField.text = ""
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            Text {
                text: qsTr("Invite to this call")
                color: win.ink
                font.pixelSize: 16
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("They get a note inviting them to this call.")
                color: win.inkDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            TextField {
                id: inviteField
                objectName: "groupCallInviteField"
                Layout.fillWidth: true
                Layout.topMargin: 6
                placeholderText: qsTr("someone@example.com")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                // The palette rather than a background of our own: a style's
                // field is built around the background it ships with.
                palette.base: win.pill
                palette.text: win.ink
                palette.placeholderText: win.inkDim
                palette.highlight: Theme.accent
                palette.highlightedText: Theme.textOnAccent
                onAccepted: invitePopup.send()
            }
            Text {
                Layout.fillWidth: true
                visible: invitePopup.sentTo !== ""
                text: qsTr("Invite sent to %1").arg(invitePopup.sentTo)
                color: Theme.positive
                font.pixelSize: 12
                elide: Text.ElideMiddle
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Done")
                    flat: true
                    palette.buttonText: win.inkDim
                    onClicked: invitePopup.close()
                }
                Button {
                    objectName: "groupCallInviteSend"
                    text: qsTr("Send invite")
                    enabled: Jid.plausible(Jid.bare(inviteField.text))
                    palette.button: Theme.accent
                    palette.buttonText: Theme.textOnAccent
                    onClicked: invitePopup.send()
                }
            }
        }
    }
}
