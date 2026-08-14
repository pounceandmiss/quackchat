import QtQuick
import Quack

// The strip that names a run of rows in a list holding more than one kind:
// "Chats" over what a search matched by name, "Messages" over what it matched
// inside them. `shown` rather than `visible` because a view writes visible on
// the header and footer it places, and two writers on one property is a loop.
Item {
    id: label
    property alias text: caption.text
    property bool shown: true

    implicitHeight: shown ? 28 : 0

    Item {
        anchors.fill: parent
        visible: label.shown

        Text {
            id: caption
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            color: Theme.textDim
            font.pixelSize: 12
            font.bold: true
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.hairline
        }
    }
}
