import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl

// The app's button, for every Button that does not draw its own. Three kinds:
// the main action is the accent pill, flat is accent text with a wash under the
// pointer, and the rest are a pill in the field colour. The main action is a
// highlighted or checked button, or a box's accept: a DialogButtonBox owns
// `highlighted` for the buttons in it, keeping it for its defaultButton alone,
// so in one the role is what says which button leads. Colours come from the palette AppWindow fills from Theme, as
// Fusion's do; this module cannot import Quack, which imports it back.
T.Button {
    id: control

    readonly property bool accented: control.highlighted || control.checked
        || (T.DialogButtonBox.buttonBox !== null
            && (T.DialogButtonBox.buttonRole === T.DialogButtonBox.AcceptRole
                || T.DialogButtonBox.buttonRole === T.DialogButtonBox.YesRole))
    readonly property bool filled: !control.flat || control.accented

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: control.filled ? 18 : 10
    verticalPadding: 8
    spacing: 6

    icon.width: 18
    icon.height: 18
    icon.color: textColor

    readonly property color textColor: !control.enabled ? control.palette.disabled.buttonText
        : control.accented ? control.palette.highlightedText
        : control.flat ? control.palette.highlight
        : control.palette.buttonText

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.textColor
    }

    background: Rectangle {
        implicitWidth: control.filled ? 88 : 0
        implicitHeight: 36
        radius: height / 2
        color: {
            if (control.accented && control.enabled)
                return control.down ? Qt.darker(control.palette.highlight, 1.15)
                                    : control.palette.highlight
            if (control.filled)
                return control.down ? Qt.darker(control.palette.button, 1.08)
                                    : control.palette.button
            const wash = control.palette.highlight
            return control.down ? Qt.rgba(wash.r, wash.g, wash.b, 0.18)
                 : control.hovered ? Qt.rgba(wash.r, wash.g, wash.b, 0.10)
                 : "transparent"
        }
        border.width: control.visualFocus ? 2 : 0
        border.color: control.palette.highlight
    }
}
