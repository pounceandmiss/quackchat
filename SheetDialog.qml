import QtQuick
import QtQuick.Controls

// A modal dialog centred in the window and kept clear of the virtual keyboard.
// Android lays the keyboard over the window rather than resizing it, so a dialog
// centred in the overlay ends up underneath it; this centres in whatever strip
// the keyboard leaves and slides as it opens and closes.
//
// The base for every dialog here that asks for typed input.
Dialog {
    id: sheet

    // Its natural width where there is room, else the window's less a margin.
    property int preferredWidth: 340

    parent: Overlay.overlay
    modal: true
    width: Math.min(preferredWidth, parent ? parent.width - 24 : preferredWidth)

    // The height of the window strip the keyboard leaves showing.
    readonly property real clearHeight: {
        if (!parent)
            return 0
        if (!Qt.inputMethod.visible) // qmllint disable missing-property
            return parent.height
        const kbTop = Qt.inputMethod.keyboardRectangle.y // qmllint disable missing-property
        return kbTop > 0 ? Math.min(kbTop, parent.height) : parent.height
    }
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: Math.max(12, Math.round((sheet.clearHeight - height) / 2))
    Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
}
