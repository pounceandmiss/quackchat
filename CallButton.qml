import QtQuick
import QtQuick.Controls
import Quack

// A round call-control button with its label underneath. Sized for a thumb,
// since the call window is the one screen you use without looking closely.
AbstractButton {
    id: btn

    // A CallIcon symbol, or "" to fall back to `glyph` - which is only for
    // dismissing a call that is already over, a close box rather than a
    // handset.
    property string symbol: ""
    property string glyph: ""
    property color fill: Theme.accent
    property int diameter: 56

    implicitWidth: Math.max(diameter, caption.implicitWidth)
    implicitHeight: diameter + caption.implicitHeight + 6

    contentItem: Item {
        Rectangle {
            id: disc
            x: (btn.width - width) / 2
            width: btn.diameter
            height: btn.diameter
            radius: width / 2
            antialiasing: true
            color: btn.fill
            opacity: btn.pressed ? 0.75 : (btn.hovered ? 0.9 : 1.0)

            CallIcon {
                anchors.centerIn: parent
                visible: btn.symbol !== ""
                width: btn.diameter * 0.5
                height: width
                symbol: btn.symbol
                color: Theme.textOnAccent
            }

            Text {
                anchors.centerIn: parent
                visible: btn.symbol === ""
                text: btn.glyph
                color: Theme.textOnAccent
                font.pixelSize: 24
            }
        }
        Text {
            id: caption
            anchors.top: disc.bottom
            anchors.topMargin: 6
            width: btn.width
            text: btn.text
            color: Theme.textDim
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }
}
