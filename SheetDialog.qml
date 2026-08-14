import QtQuick
import QtQuick.Controls

// A modal dialog centred in the window and kept clear of the virtual keyboard.
// Android lays the keyboard over the window rather than resizing it, so a dialog
// centred in the overlay ends up underneath it; this centres in whatever strip
// the keyboard leaves and slides as it opens and closes.
//
// The base for every dialog in the app, typed-into or not: the ones that only
// ask a yes/no get the centring and the width clamp, and nothing else applies.
Dialog {
    id: sheet

    // Its natural width where there is room, else the window's less a margin.
    property int preferredWidth: 340

    parent: Overlay.overlay
    modal: true
    width: Math.min(preferredWidth, parent ? parent.width - 24 : preferredWidth)

    // Android measures the keyboard in physical pixels while the scene it
    // covers is laid out in device-independent ones; every other platform
    // reports the rectangle in the scene's own units already.
    readonly property real keyboardScale: parent ? parent.Screen.devicePixelRatio : 1

    // The keyboard's top edge in window coordinates, or -1 while it is down.
    property real keyboardTop: {
        if (!Qt.inputMethod.visible) // qmllint disable missing-property
            return -1
        const top = Qt.inputMethod.keyboardRectangle.y // qmllint disable missing-property
        return Qt.platform.os === "android" ? top / sheet.keyboardScale : top
    }

    // The height of the window strip the keyboard leaves showing.
    readonly property real clearHeight: {
        if (!parent)
            return 0
        return sheet.keyboardTop > 0 ? Math.min(sheet.keyboardTop, parent.height)
                                     : parent.height
    }
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: Math.max(12, Math.round((sheet.clearHeight - height) / 2))
    Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
}
