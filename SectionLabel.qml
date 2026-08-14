import QtQuick
import Quack

// The strip that names a run of rows in a list that holds more than one kind:
// "Chats" over the conversations a search matched by name, "Messages" over what
// it matched inside them. Sized to sit between rows rather than over them, so
// the list reads as one column with breaks in it.
//
// A heading comes and goes with what is under it, and `shown` is how it goes:
// a view writes `visible` on the header and footer it places, so binding that
// property here would be two writers on one value. Nothing of ours touches it -
// the strip stands its height down to nothing instead, and what it draws hangs
// off an item the view has no say over.
Item {
    id: label
    property alias text: caption.text
    property bool shown: true

    implicitHeight: shown ? 28 : 0
    height: implicitHeight

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
