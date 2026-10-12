import QtQuick
import QtQuick.Controls

// A modal dialog centred in the window and kept clear of the virtual keyboard.
// The overlay a dialog sits in spans the whole window, keyboard included, so a
// dialog centred there ends up underneath it; this centres in whatever strip
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

    // The keyboard's top edge in the overlay's coordinates, or -1 while it is
    // down.
    readonly property real keyboardTop: Keyboard.height > 0 && parent
        ? parent.mapFromItem(null, 0, parent.Window.height - Keyboard.height).y : -1

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
