import QtQuick
import QtQuick.Layouts
import Quack

// A titled panel in a settings column: whatever is put inside it is laid out in
// a column with the panel sized to hold it. Meant to be a child of a
// ColumnLayout, whose width it takes.
Rectangle {
    default property alias content: cardColumn.data

    Layout.fillWidth: true
    implicitHeight: cardColumn.implicitHeight + 28
    color: Theme.surface
    radius: 12
    border.width: 1
    border.color: Theme.hairline

    ColumnLayout {
        id: cardColumn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 10
    }
}
