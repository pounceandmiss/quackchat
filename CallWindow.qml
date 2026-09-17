pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Quack

// The call itself: who you are talking to, how it is going, and the way out.
// AppWindows builds one per CallsModel row and destroys it when that row goes,
// but only ever shows one at a time - a call still ringing at you shows an
// IncomingCallDialog instead, and takes this window when it is answered.
//
// Nothing here decides anything about the call - it reads `state` off the model
// and calls back into it. The one piece of local policy is when to let go: a
// call that ended closes itself after a beat, one that failed waits, so the
// reason stays readable and "Call again" is there to press.
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

    // Local intent to send camera. Starts true whenever this call has a
    // preview ring (i.e. video was negotiated); the toggle drives setVideo.
    property bool cameraOn: sendingVideo

    // The dialog has a call while it rings, so this window is not normally up
    // then - but leave() is the single exit path, and a ringing call is
    // declined rather than hung up.
    readonly property bool ringingIn: state === "incoming"

    // Video needs a taller layout to fit the button row below it; sized
    // before <VideoTrack>/<VideoPreview> land by offeredVideo (incoming) or
    // grown once sendingVideo/hasRemoteVideo turns true (outgoing).
    readonly property bool videoActive: win.offeredVideo || win.hasRemoteVideo || win.sendingVideo

    width: Theme.mobile ? Screen.width : videoActive ? 480 : 380
    height: Theme.mobile ? Screen.height : videoActive ? 640 : 460
    minimumWidth: Theme.mobile ? 0 : videoActive ? 400 : 320
    minimumHeight: Theme.mobile ? 0 : videoActive ? 560 : 380
    title: qsTr("Call — %1").arg(peer)
    color: Theme.background

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
        interval: 900
        onTriggered: App.calls.dismiss(win.account, win.sid)
    }

    readonly property string statusText: {
        switch (win.state) {
        case "calling":    return qsTr("Calling…")
        case "ringing":    return qsTr("Ringing…")
        case "incoming":   return qsTr("Incoming call")
        case "connecting": return qsTr("Connecting…")
        case "active":     return qsTr("Connected")
        case "ended":      return qsTr("Call ended")
        case "failed":     return qsTr("Call failed")
        default:           return ""
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 18 + SafeArea.margins.top
        anchors.bottomMargin: 18 + SafeArea.margins.bottom
        spacing: 0

        Item { Layout.fillHeight: true }

        // Remote video when it is flowing, the avatar otherwise, with a
        // mirrored self-view tucked bottom-right while the camera is on.
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: win.hasRemoteVideo
            Layout.preferredWidth: win.hasRemoteVideo ? -1 : 96
            Layout.preferredHeight: win.hasRemoteVideo ? 260 : 96

            Avatar {
                anchors.centerIn: parent
                width: 96; height: 96
                visible: !win.hasRemoteVideo
                account: win.account
                jid: win.peer
                label: win.peer
                initialsPixelSize: 38
            }

            VideoOutput {
                id: remoteOut
                anchors.fill: parent
                visible: win.hasRemoteVideo
                fillMode: VideoOutput.PreserveAspectFit
                VideoSurface {
                    videoSink: remoteOut.videoSink
                    channel: win.remoteVideo || ({})
                }
            }

            VideoOutput {
                id: previewOut
                width: 96; height: 72
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 6
                visible: win.sendingVideo
                fillMode: VideoOutput.PreserveAspectCrop
                transform: Scale { origin.x: previewOut.width / 2; xScale: -1 }
                VideoSurface {
                    videoSink: previewOut.videoSink
                    channel: win.preview || ({})
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 14
            text: win.peer
            color: Theme.textPrimary
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: win.statusText
            color: win.state === "failed" ? Theme.negative : Theme.textDim
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
        }

        // <Failed> carries why; <Warning> is non-fatal and the call carries on.
        Text {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: win.state === "failed" ? win.reason : win.warning
            visible: text !== ""
            color: win.state === "failed" ? Theme.negative : Theme.warning
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        // Mic and speaker are machine-wide in tacky, so these knobs outlive the
        // call and are shared with every other one that is running.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 16
            visible: !win.terminal
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Item { Layout.fillWidth: true }

            // Retry after a failure, in the same window - the new call's
            // <Outgoing> row spawns its own.
            CallButton {
                visible: win.state === "failed"
                iconPath: Icons.call
                fill: Theme.positive
                text: qsTr("Call again")
                onClicked: {
                    App.calls.start(win.account, win.peer)
                    App.calls.dismiss(win.account, win.sid)
                }
            }

            // Camera on/off, shown once this call has video negotiated.
            CallButton {
                visible: !win.terminal && (win.sendingVideo || win.preview)
                iconPath: win.cameraOn ? Icons.videoCam : Icons.videoCamOff
                fill: win.cameraOn ? Theme.positive : Theme.textDim
                text: win.cameraOn ? qsTr("Camera off") : qsTr("Camera on")
                onClicked: {
                    win.cameraOn = !win.cameraOn
                    App.calls.setVideo(win.account, win.sid, win.cameraOn)
                }
            }

            CallButton {
                iconPath: win.terminal ? Icons.close : Icons.callEnd
                fill: win.terminal ? Theme.textDim : Theme.negative
                text: win.terminal ? qsTr("Close")
                                   : (win.ringingIn ? qsTr("Decline") : qsTr("Hang up"))
                onClicked: win.leave()
            }

            Item { Layout.fillWidth: true }
        }
    }
}
