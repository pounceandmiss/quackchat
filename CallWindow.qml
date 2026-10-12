pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtMultimedia
import Quack

// One call, from its ring to its end. Voice and ringing are centred in the
// theme; once the peer's video flows it fills the window, with the controls on
// scrims that fade while the mouse is still. Same design as Android's
// CallActivity, which runs calls there instead of this window.
//
// AppWindows builds one per CallsModel row and shows it while the call rings
// or is the live one. Nothing here decides anything about the call - it reads
// `state` off the model and calls back into it. The one piece of local policy
// is when to let go: a call that ended closes itself after a beat, one that
// failed waits, so the reason stays readable and "Call again" is there to
// press.
ApplicationWindow {
    id: win

    required property string sid
    required property string account
    required property string peer
    required property string state
    required property string direction
    required property string warning
    required property string reason
    required property bool terminal
    required property bool offeredVideo
    required property bool hasRemoteVideo
    required property bool sendingVideo
    required property var remoteVideo
    required property var preview
    required property double activeSince

    // Local intent to send camera. Starts true whenever this call has a
    // preview ring (i.e. video was negotiated); the toggle drives setVideo.
    property bool cameraOn: sendingVideo

    readonly property bool ringingIn: state === "incoming"

    readonly property bool videoLook: win.hasRemoteVideo && !win.terminal && !win.ringingIn

    // Sized for video before <VideoTrack>/<VideoPreview> land.
    readonly property bool videoActive: win.offeredVideo || win.hasRemoteVideo || win.sendingVideo

    // Read once; a rename mid-call can wait.
    property string displayName: ""
    Component.onCompleted: {
        const list = App.chatListFor(win.account)
        const entry = list ? list.entryFor(win.peer) : ({})
        win.displayName = entry.name ?? ""
    }
    readonly property string shownName: win.displayName !== "" ? win.displayName : win.peer

    width: videoActive ? 520 : 400
    height: videoActive ? 720 : 600
    minimumWidth: 340
    minimumHeight: 480
    title: win.ringingIn ? qsTr("Incoming call from %1").arg(win.peer)
                         : qsTr("Call — %1").arg(win.peer)
    color: win.videoLook ? "black" : Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }
    Shortcut { sequence: "Escape"; onActivated: win.leave() }

    // The single exit path: end the call if it is still running, then let the
    // model drop the row, which is what actually destroys this window.
    function leave() {
        if (win.terminal)
            App.calls.dismiss(win.account, win.sid)
        else if (win.ringingIn)
            App.calls.reject(win.account, win.sid)
        else
            App.calls.hangup(win.account, win.sid)
    }

    onClosing: win.leave()

    // A finished call has nothing left to show once it has been read, and a
    // failed one stays up so it can be. The second clause is for a call that
    // was never shown at all - declined while ringing, or ended on another
    // device: nobody is reading that, and the row would sit in the model
    // forever waiting on a window that never appeared.
    Timer {
        running: win.state === "ended" || (win.terminal && !win.visible)
        interval: 1500
        onTriggered: App.calls.dismiss(win.account, win.sid)
    }

    readonly property string statusText: {
        switch (win.state) {
        case "calling":    return qsTr("Calling…")
        case "ringing":    return qsTr("Ringing…")
        case "incoming":   return win.offeredVideo ? qsTr("Incoming video call")
                                                   : qsTr("Incoming call")
        case "connecting": return qsTr("Connecting…")
        case "active":     return qsTr("Connected")
        case "failed":     return qsTr("Call failed")
        case "ended":      return win.endText
        default:           return ""
        }
    }

    readonly property string endText: {
        switch (win.reason) {
        case "declined":  return qsTr("Declined")
        case "busy":      return qsTr("Busy")
        case "retracted": return qsTr("Call cancelled")
        case "elsewhere": return qsTr("Answered on another device")
        case "timeout":   return qsTr("No answer")
        case "reset":     return qsTr("Connection lost")
        default:          return qsTr("Call ended")
        }
    }

    property string durationText: ""
    function tick() {
        if (win.activeSince <= 0) {
            win.durationText = ""
            return
        }
        const secs = Math.max(0, Math.floor((Date.now() * 1000 - win.activeSince) / 1e6))
        const h = Math.floor(secs / 3600)
        const m = Math.floor(secs / 60) % 60
        const s = secs % 60
        const two = (n) => (n < 10 ? "0" : "") + n
        win.durationText = (h > 0 ? h + ":" + two(m) : m) + ":" + two(s)
    }
    onActiveSinceChanged: win.tick()
    Timer {
        running: win.activeSince > 0 && !win.terminal
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: win.tick()
    }

    readonly property string statusLine: win.durationText !== "" && !win.terminal
        ? win.statusText + "  ·  " + win.durationText : win.statusText

    // Over video the overlays fade while the mouse is still; the audio popup
    // holds them up.
    property bool overlaysShown: true
    Timer {
        id: overlayIdle
        interval: 4000
        running: win.videoLook && win.overlaysShown && !devices.opened
        onTriggered: win.overlaysShown = false
    }
    function poke() {
        win.overlaysShown = true
        overlayIdle.restart()
    }
    onVideoLookChanged: win.overlaysShown = true
    readonly property bool overlaysVisible: !win.videoLook || win.overlaysShown

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: if (win.videoLook) win.poke()
    }

    VideoOutput {
        id: remoteOut
        anchors.fill: parent
        visible: win.videoLook
        fillMode: VideoOutput.PreserveAspectCrop
        VideoSurface {
            videoSink: remoteOut.videoSink
            channel: win.remoteVideo || ({})
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: controls.height
        visible: !win.videoLook
        spacing: 0

        Item { Layout.fillHeight: true }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 140
            Layout.preferredHeight: 140
            account: win.account
            jid: win.peer
            label: win.shownName
            initialsPixelSize: 56
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 20
            text: win.shownName
            color: Theme.textPrimary
            font.pixelSize: 28
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 2
            visible: win.shownName !== win.peer
            text: win.peer
            color: Theme.textDim
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 10
            text: win.statusLine
            color: Theme.textDim
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 8
            visible: win.state === "failed" && win.reason !== ""
            text: win.reason
            color: Theme.negative
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 8
            visible: win.ringingIn
            text: qsTr("using account %1").arg(win.account)
            color: Theme.textDim
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        Item { Layout.fillHeight: true }
    }

    Rectangle {
        id: videoTop
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: topColumn.implicitHeight + 56 + SafeArea.margins.top
        visible: win.videoLook && opacity > 0
        opacity: win.overlaysVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.6) }
            GradientStop { position: 1.0; color: "transparent" }
        }

        ColumnLayout {
            id: topColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            anchors.topMargin: 16 + SafeArea.margins.top
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: win.shownName
                color: "white"
                font.pixelSize: 19
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: win.statusLine
                color: Qt.rgba(1, 1, 1, 0.85)
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    // <Warning>, until the call is up again.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: (win.videoLook ? videoTop.height - 28 : 16) + SafeArea.margins.top
        visible: win.warning !== "" && !win.terminal
        width: warningText.implicitWidth + 28
        height: warningText.implicitHeight + 12
        radius: height / 2
        color: Qt.rgba(0.12, 0.12, 0.12, 0.8)
        Text {
            id: warningText
            anchors.centerIn: parent
            text: win.warning
            color: "white"
            font.pixelSize: 12
        }
    }

    Rectangle {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: controlColumn.implicitHeight + 40 + 20 + SafeArea.margins.bottom
        opacity: win.overlaysVisible ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: win.videoLook ? Qt.rgba(0, 0, 0, 0.65)
                                                               : "transparent" }
        }

        // The theme over voice, translucent white over video.
        function fillFor(on) {
            return win.videoLook ? (on ? "white" : Qt.rgba(1, 1, 1, 0.2))
                                 : (on ? Theme.accent : Theme.surface)
        }
        function glyphFor(on) {
            return win.videoLook ? (on ? "#202020" : "white")
                                 : (on ? Theme.textOnAccent : Theme.textPrimary)
        }
        readonly property color captionColor: win.videoLook ? "white" : Theme.textDim

        ColumnLayout {
            id: controlColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20 + SafeArea.margins.bottom
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                visible: win.ringingIn
                spacing: 0

                Item { Layout.fillWidth: true }
                CallButton {
                    diameter: 64
                    iconPath: Icons.callEnd
                    fill: Theme.negative
                    glyphColor: "white"
                    captionColor: controls.captionColor
                    text: qsTr("Decline")
                    onClicked: App.calls.reject(win.account, win.sid)
                }
                Item { Layout.preferredWidth: 80 }
                CallButton {
                    diameter: 64
                    iconPath: Icons.call
                    fill: "#43a047"
                    glyphColor: "white"
                    captionColor: controls.captionColor
                    text: qsTr("Answer")
                    onClicked: App.calls.accept(win.account, win.sid)
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: !win.ringingIn && !win.terminal
                spacing: 18

                Item { Layout.fillWidth: true }

                CallButton {
                    objectName: "muteToggle"
                    iconPath: App.audio.captureMuted ? Icons.micOff : Icons.mic
                    fill: controls.fillFor(App.audio.captureMuted)
                    glyphColor: controls.glyphFor(App.audio.captureMuted)
                    captionColor: controls.captionColor
                    text: App.audio.captureMuted ? qsTr("Unmute") : qsTr("Mute")
                    onClicked: {
                        App.audio.setCaptureMuted(!App.audio.captureMuted)
                        win.poke()
                    }
                }

                // Turning the camera off keeps the preview ring, so
                // sendingVideo stays true.
                CallButton {
                    objectName: "cameraToggle"
                    visible: win.sendingVideo
                    iconPath: win.cameraOn ? Icons.videoCam : Icons.videoCamOff
                    fill: controls.fillFor(!win.cameraOn)
                    glyphColor: controls.glyphFor(!win.cameraOn)
                    captionColor: controls.captionColor
                    text: qsTr("Camera")
                    onClicked: {
                        win.cameraOn = !win.cameraOn
                        App.calls.setVideo(win.account, win.sid, win.cameraOn)
                        win.poke()
                    }
                }

                // Machine-wide in tacky: shared with every other call.
                CallButton {
                    objectName: "audioSettings"
                    iconPath: Icons.volumeUp
                    fill: controls.fillFor(devices.opened)
                    glyphColor: controls.glyphFor(devices.opened)
                    captionColor: controls.captionColor
                    text: qsTr("Audio")
                    onClicked: devices.opened ? devices.close() : devices.open()
                }

                CallButton {
                    iconPath: Icons.callEnd
                    fill: Theme.negative
                    glyphColor: "white"
                    captionColor: controls.captionColor
                    text: qsTr("End")
                    onClicked: win.leave()
                }

                Item { Layout.fillWidth: true }
            }

            // The retried call's <Outgoing> row spawns its own window.
            RowLayout {
                Layout.fillWidth: true
                visible: win.terminal
                spacing: 18

                Item { Layout.fillWidth: true }
                CallButton {
                    visible: win.state === "failed"
                    iconPath: Icons.call
                    fill: Theme.positive
                    glyphColor: "white"
                    text: qsTr("Call again")
                    onClicked: {
                        App.calls.start(win.account, win.peer)
                        App.calls.dismiss(win.account, win.sid)
                    }
                }
                CallButton {
                    iconPath: Icons.close
                    fill: Theme.textDim
                    glyphColor: "white"
                    text: qsTr("Close")
                    onClicked: win.leave()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    Item {
        id: selfTile
        width: 132; height: 99
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        anchors.topMargin: 16 + SafeArea.margins.top
            + (win.videoLook && win.overlaysVisible ? videoTop.height - 40 : 0)
        visible: win.sendingVideo && win.cameraOn && !win.terminal
        Behavior on anchors.topMargin { NumberAnimation { duration: 200 } }

        VideoOutput {
            id: previewOut
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
            transform: Scale { origin.x: previewOut.width / 2; xScale: -1 }
            VideoSurface {
                videoSink: previewOut.videoSink
                channel: win.preview || ({})
            }
        }
        // Rounded as Avatar does it.
        ShaderEffectSource {
            id: selfSource
            anchors.fill: parent
            sourceItem: previewOut
            hideSource: true
            live: true
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: selfSource
            maskEnabled: true
            maskSource: selfMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        Item {
            id: selfMask
            anchors.fill: parent
            layer.enabled: true
            layer.smooth: true
            layer.samples: 4
            visible: false
            Rectangle { anchors.fill: parent; radius: 12; antialiasing: true }
        }
    }

    Popup {
        id: devices
        x: 16
        width: win.width - 32
        y: win.height - height - controls.height - 8
        padding: 14
        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.hairline
        }

        contentItem: ColumnLayout {
            spacing: 8

            AudioLevelRow {
                Layout.fillWidth: true
                mutedIcon: Icons.micOff
                liveIcon: Icons.mic
                label: qsTr("Microphone")
                devices: App.audio.captureDevices
                deviceId: App.audio.captureDevice
                volume: App.audio.captureVolume
                muted: App.audio.captureMuted
                onDevicePicked: (id) => App.audio.setCaptureDevice(id)
                onVolumePicked: (v) => App.audio.setCaptureVolume(v)
                onMuteToggled: (m) => App.audio.setCaptureMuted(m)
            }

            AudioLevelRow {
                Layout.fillWidth: true
                label: qsTr("Speaker")
                devices: App.audio.playbackDevices
                deviceId: App.audio.playbackDevice
                volume: App.audio.playbackVolume
                muted: App.audio.playbackMuted
                onDevicePicked: (id) => App.audio.setPlaybackDevice(id)
                onVolumePicked: (v) => App.audio.setPlaybackVolume(v)
                onMuteToggled: (m) => App.audio.setPlaybackMuted(m)
            }
        }
    }
}
