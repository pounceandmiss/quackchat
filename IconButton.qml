import QtQuick
import QtQuick.Controls
import Quack

// A flat glyph button: themed text with a rounded hover wash and no chrome of
// its own. The window and chat headers are built from these.
ToolButton {
    id: btn
    property color glyphColor: Theme.textPrimary

    font.pixelSize: 18
    // Keep every glyph button a comfortable touch target regardless of how
    // small its glyph renders (headers are 60px tall, so 40px fits).
    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 40)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, 40)
    contentItem: Text {
        text: btn.text
        color: btn.glyphColor
        font: btn.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        color: btn.hovered ? Theme.menuHover : "transparent"
        radius: 6
    }
}
