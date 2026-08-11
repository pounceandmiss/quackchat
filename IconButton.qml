import QtQuick
import QtQuick.Controls
import Quack

// A flat glyph button: themed text with a rounded hover wash and no chrome of
// its own. The window and chat headers are built from these.
ToolButton {
    id: btn
    property color glyphColor: Theme.textPrimary
    // An Icons path to draw. Left empty the button falls back on `text`, which
    // is what the emoji buttons still use - a colour font paints itself and
    // would ignore glyphColor anyway.
    property string iconPath: ""
    // Tracks the default font size, so a drawn button sits where a typed one did.
    property real iconSize: 18

    font.pixelSize: 18
    // Keep every glyph button a comfortable touch target regardless of how
    // small its glyph renders (headers are 60px tall, so 40px fits).
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 40)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, 40)
    contentItem: Item {
        implicitWidth: btn.iconPath === "" ? label.implicitWidth : btn.iconSize
        implicitHeight: btn.iconPath === "" ? label.implicitHeight : btn.iconSize
        Text {
            id: label
            anchors.fill: parent
            visible: btn.iconPath === ""
            text: btn.text
            color: btn.glyphColor
            font: btn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Glyph {
            anchors.centerIn: parent
            visible: btn.iconPath !== ""
            path: btn.iconPath
            color: btn.glyphColor
            size: btn.iconSize
        }
    }
    background: Rectangle {
        color: btn.hovered ? Theme.menuHover : "transparent"
        radius: 6
    }
}
