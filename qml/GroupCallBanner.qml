import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The strip under a room's header that says the room has a call going: how
// many are in it and a way in, or - once we are in - a way back to its window.
// Hidden when there is nothing to say, which is nearly always.
Rectangle {
    id: banner

    property GroupCall call: null

    readonly property bool active: banner.call ? banner.call.active : false
    readonly property bool inCall: banner.call ? banner.call.inCall : false
    readonly property int count: banner.call ? banner.call.count : 0

    visible: banner.active || banner.inCall
    implicitHeight: visible ? row.implicitHeight + 20 : 0
    color: banner.inCall ? Qt.rgba(Theme.positive.r, Theme.positive.g, Theme.positive.b, 0.12)
                         : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.14)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 12
        spacing: 12

        Rectangle {
            Layout.preferredWidth: 34; Layout.preferredHeight: 34; radius: 17
            color: banner.inCall ? Theme.positive : Theme.accent
            Glyph {
                anchors.centerIn: parent
                path: Icons.call
                color: banner.inCall ? "#ffffff" : Theme.textOnAccent
                size: 18
            }
            SequentialAnimation on scale {
                running: banner.active && !banner.inCall
                loops: Animation.Infinite
                NumberAnimation { to: 1.08; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                Layout.fillWidth: true
                objectName: "groupCallBannerTitle"
                text: banner.inCall ? qsTr("You're in this room's call")
                                    : qsTr("Call in progress")
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("%n person(s) in the call", "", banner.count)
                color: Theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        // Not in: join with the mic, or with the camera as well. In: back to
        // the window, or out.
        IconButton {
            visible: !banner.inCall
            Accessible.name: qsTr("Join with video")
            iconPath: Icons.videoCam
            iconSize: 20
            glyphColor: Theme.accentDeep
            onClicked: banner.call.join(true)
        }
        Button {
            objectName: "groupCallBannerJoin"
            visible: !banner.inCall
            text: qsTr("Join")
            palette.button: Theme.accent
            palette.buttonText: Theme.textOnAccent
            font.bold: true
            onClicked: banner.call.join(false)
        }
        Button {
            objectName: "groupCallBannerOpen"
            visible: banner.inCall
            text: qsTr("Open")
            palette.button: Theme.positive
            palette.buttonText: "#ffffff"
            font.bold: true
            onClicked: AppWindows.raiseGroupCall(banner.call.account, banner.call.jid)
        }
        IconButton {
            visible: banner.inCall
            Accessible.name: qsTr("Leave call")
            iconPath: Icons.callEnd
            iconSize: 20
            glyphColor: Theme.negative
            onClicked: banner.call.leave()
        }
    }
}
