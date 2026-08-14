import QtQuick
import Quack

// The strip that names a run of rows in a list that holds more than one kind:
// "Chats" over the conversations a search matched by name, "Messages" over what
// it matched inside them. Sized to sit between rows rather than over them, so
// the list reads as one column with breaks in it.
Item {
    id: label
    property alias text: caption.text

    implicitHeight: 28

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
