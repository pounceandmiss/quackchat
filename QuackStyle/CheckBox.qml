import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T

// The app's check box: a rounded square that fills with the accent and shows a
// tick when checked, the message-selection check's look in a box rather than a
// circle. Colours from the palette, as Button's.
T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: 8

    readonly property bool marked: control.checkState !== Qt.Unchecked
    readonly property color ink: control.enabled ? control.palette.highlight
                                                 : control.palette.placeholderText

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 5
        color: control.marked ? control.ink : "transparent"
        border.width: 2
        border.color: control.marked || control.visualFocus ? control.ink
                                                            : control.palette.placeholderText
        opacity: control.down ? 0.8 : 1

        // Icons.check, which this module cannot import: the 24x24 Material tick.
        Shape {
            anchors.centerIn: parent
            width: 16
            height: 16
            visible: control.checkState === Qt.Checked
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                fillColor: control.palette.highlightedText
                strokeWidth: -1
                scale: Qt.size(16 / 24, 16 / 24)
                PathSvg { path: "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z" }
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 2
            color: control.palette.highlightedText
            visible: control.checkState === Qt.PartiallyChecked
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? control.palette.windowText : control.palette.disabled.windowText
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }
}
