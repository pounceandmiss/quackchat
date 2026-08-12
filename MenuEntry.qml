import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// A row in one of the app's menus: its label, and a trailing note for whatever
// the entry has to add - a keyboard shortcut, or a tick where it is a setting.
MenuItem {
    id: entry

    // Whether this menu has any use for the entry. An entry it has none for
    // collapses rather than merely hiding, or the column would hold a blank
    // slot where it would have been. Not `visible ? 40 : 0`: a closed menu is
    // invisible and so is everything in it, which would leave every entry
    // measuring zero until the menu opened.
    property bool offered: true
    property string trailing: ""
    property color labelColor: Theme.textPrimary

    visible: entry.offered
    height: entry.offered ? 40 : 0
    contentItem: RowLayout {
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: entry.text
            color: entry.labelColor
            font.pixelSize: 14
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        Text {
            text: entry.trailing
            color: Theme.textDim
            font.pixelSize: 12
            rightPadding: 8
        }
    }
    background: Rectangle {
        color: entry.highlighted ? Theme.menuHover : "transparent"
        radius: 6
    }
}
