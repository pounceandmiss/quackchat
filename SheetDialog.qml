import QtQuick
import QtQuick.Controls

// A modal dialog centred in the window and kept clear of the virtual keyboard.
// The overlay a dialog sits in spans the whole window, keyboard included, so a
// dialog centred there ends up underneath it; this centres in whatever strip
// the keyboard leaves and slides as it opens and closes.
//
// Where that strip is shorter than the dialog, the dialog shrinks to it and its
// content scrolls, title and buttons staying put; the focused field is kept in
// view.
//
// The base for every dialog in the app, typed-into or not: the ones that only
// ask a yes/no get the centring and the width clamp, and nothing else applies.
Dialog {
    id: sheet

    // Its natural width where there is room, else the window's less a margin.
    property int preferredWidth: 340

    // The content, which fills the dialog where it fits and scrolls where it
    // does not.
    default property alias content: body.data

    // The tallest it may be: the strip less a margin. A sheet that sets its
    // own height caps it at this.
    readonly property real maxHeight: Math.max(160, clearHeight - 24)

    parent: Overlay.overlay
    modal: true
    width: Math.min(preferredWidth, parent ? parent.width - 24 : preferredWidth)
    height: Math.min(implicitHeight, maxHeight)

    contentItem: Flickable {
        id: flick
        implicitHeight: body.implicitHeight
        contentWidth: width
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollIndicator.vertical: ScrollIndicator {}

        Item {
            id: body
            width: flick.width
            // At least the dialog's height, so content that fills it still
            // can; more when the content needs it, which is what scrolls.
            height: Math.max(flick.height, implicitHeight)
            implicitHeight: {
                let h = 0
                for (const child of body.children)
                    h = Math.max(h, child.implicitHeight)
                return h
            }
        }

        // Scrolls the focused field into view, on focus and whenever the
        // dialog shrinks under it, as it does when the keyboard rises.
        readonly property Item focused: Window.activeFocusItem
        function reveal() {
            let y = flick.contentY
            let inBody = false
            for (let p = flick.focused; p !== null && !inBody; p = p.parent)
                inBody = p === body
            if (inBody) {
                const top = flick.focused.mapToItem(body, 0, 0).y
                y = Math.max(Math.min(y, top), top + flick.focused.height - flick.height)
            }
            flick.contentY = Math.max(0, Math.min(y, flick.contentHeight - flick.height))
        }
        onFocusedChanged: reveal()
        onHeightChanged: reveal()
        onContentHeightChanged: reveal()
    }

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
