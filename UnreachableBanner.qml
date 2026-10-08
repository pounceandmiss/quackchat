import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The strip over a room's composer that says the last message did not go out
// because some members could not be encrypted for, who they are, and the two
// ways past it. tacky keeps the list until a send goes through; closing the
// strip only stops showing it.
Rectangle {
    id: banner

    property OmemoChat omemo: null
    signal reviewKeys

    readonly property var members: banner.omemo ? banner.omemo.unreachable : []

    visible: banner.members.length > 0
    implicitHeight: visible ? column.implicitHeight + 20 : 0
    color: Qt.rgba(Theme.negative.r, Theme.negative.g, Theme.negative.b, 0.10)

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.hairline
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Glyph {
                Layout.alignment: Qt.AlignTop
                path: Icons.lock
                color: Theme.negative
                size: 18
            }
            Text {
                objectName: "unreachableText"
                Layout.fillWidth: true
                text: qsTr("Not sent: no usable encryption key for %1.")
                      .arg(banner.members.map(m => m.jid).join(", "))
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
            IconButton {
                Layout.alignment: Qt.AlignTop
                objectName: "unreachableDismiss"
                iconPath: Icons.close
                iconSize: 16
                implicitWidth: 30
                implicitHeight: 30
                Accessible.name: qsTr("Dismiss")
                glyphColor: Theme.textDim
                onClicked: banner.omemo.dismissUnreachable()
            }
        }
        RowLayout {
            Layout.leftMargin: 28
            spacing: 8
            Button {
                objectName: "unreachableReview"
                text: qsTr("Review keys")
                flat: true
                onClicked: banner.reviewKeys()
            }
            Button {
                objectName: "unreachableTurnOff"
                text: qsTr("Turn off encryption")
                flat: true
                palette.buttonText: Theme.warning
                onClicked: {
                    banner.omemo.enabled = false
                    banner.omemo.dismissUnreachable()
                }
            }
        }
    }
}
