import QtQuick
import QtQuick.Controls
import Quack

// Where the emoji picker floats. On desktop, a card over the spot it was
// asked for from (the reaction bar, which it takes the place of), kept inside
// the window. On touch, a sheet up from the bottom edge across the width, the
// way a phone's own pickers come, which rides up over the keyboard when the
// search field brings one.
Popup {
    id: pop
    objectName: "emojiPopup"

    signal chosen(string emoji)

    // The sheet from the bottom edge rather than the card. Settable, since a
    // test has no phone to run on.
    property bool sheet: Theme.mobile

    // Where it was asked for, in the overlay's coordinates. Desktop only.
    property point anchor: Qt.point(0, 0)

    function openAt(scenePoint) {
        pop.anchor = parent ? parent.mapFromItem(null, scenePoint.x, scenePoint.y)
                            : scenePoint
        pop.open()
    }

    parent: Overlay.overlay
    modal: true
    dim: pop.sheet
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // The keyboard's top edge in the overlay's coordinates, or the overlay's
    // foot while it is down: what the sheet sits on. See SheetDialog.
    readonly property real floor: Keyboard.height > 0 && parent
        ? Math.min(parent.height, parent.mapFromItem(null, 0, parent.Window.height - Keyboard.height).y)
        : (parent ? parent.height : 0)

    // 0 shut, 1 up: the sheet's slide, kept apart from y so y stays a binding
    // the keyboard can move.
    property real slide: 1

    width: pop.sheet ? (parent ? parent.width : 0)
                        : Math.min(360, parent ? parent.width - 16 : 360)
    height: {
        if (!parent)
            return 0
        if (!pop.sheet)
            return Math.min(430, parent.height - 16)
        // Some of the chat stays in view above it, and with the keyboard up
        // what the keyboard leaves.
        const tall = Math.min(Math.round(parent.height * 0.6), 480) + bottomPadding
        return Math.min(tall, pop.floor - parent.SafeArea.margins.top - 24)
    }
    x: pop.sheet ? 0
       : Math.max(8, Math.min(pop.anchor.x, (parent ? parent.width : 0) - width - 8))
    y: pop.sheet ? pop.floor - height * pop.slide
       : Math.max(8, Math.min(pop.anchor.y, (parent ? parent.height : 0) - height - 8))
    Behavior on y {
        enabled: pop.sheet && pop.opened
        NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
    }

    // Past the gesture bar, unless the keyboard already covers it.
    bottomPadding: pop.sheet && Keyboard.height === 0 && parent
        ? parent.SafeArea.margins.bottom : 0

    // The grip's room on touch.
    topPadding: pop.sheet ? 12 : 0

    background: Rectangle {
        color: Theme.surface
        border.color: pop.sheet ? "transparent" : Theme.hairline
        // Square along the bottom edge on touch, which it sits on.
        radius: pop.sheet ? 0 : 12
        topLeftRadius: pop.sheet ? 16 : 12
        topRightRadius: pop.sheet ? 16 : 12
        // A grip, to say the sheet is a sheet.
        Rectangle {
            visible: pop.sheet
            anchors.horizontalCenter: parent.horizontalCenter
            y: 6
            width: 36
            height: 4
            radius: 2
            color: Theme.hairline
        }
    }

    enter: Transition {
        NumberAnimation {
            property: pop.sheet ? "slide" : "opacity"
            from: 0; to: 1; duration: pop.sheet ? 220 : 110
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"; from: pop.sheet ? 1 : 0.92; to: 1
            duration: 150; easing.type: Easing.OutCubic
        }
    }
    exit: Transition {
        NumberAnimation {
            property: pop.sheet ? "slide" : "opacity"
            from: 1; to: 0; duration: pop.sheet ? 160 : 90
            easing.type: Easing.InCubic
        }
    }

    // Built on the first open rather than with the page: the picker's model
    // wants the whole sifted catalogue, and asked for while the window loads
    // it is sifted then, on the UI thread, instead of by main.cpp's preload.
    contentItem: Loader {
        id: picker
        active: false
        sourceComponent: EmojiPicker {
            touch: pop.sheet
            onChosen: (emoji) => {
                pop.close()
                pop.chosen(emoji)
            }
        }
    }

    // A keyboard up for the composer would leave only a sliver for the sheet;
    // the field brings it back when tapped.
    onAboutToShow: {
        picker.active = true
        if (pop.sheet)
            Qt.inputMethod.hide() // qmllint disable missing-property
        pop.slide = pop.sheet ? 0 : 1
    }
    onOpened: picker.item.reset()

    // WORKAROUND, as in FullScreenSheet: a modal popup lets drags through to
    // the pointer handlers behind it, so flicking the grid would swipe the
    // message under it into a reply. `covers` is taken out of reach meanwhile.
    property Item covers: null
    Binding {
        target: pop.covers
        property: "enabled"
        value: false
        when: pop.covers !== null && pop.visible
        restoreMode: Binding.RestoreBindingOrValue
    }
}
