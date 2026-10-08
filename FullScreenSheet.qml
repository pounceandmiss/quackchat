import QtQuick
import QtQuick.Controls
import Quack

// A page over the whole window, which is how mobile hosts what desktop gives a
// window of its own. It is parented to the overlay, past the inset the window
// applies to its own content, so it pads itself instead: the background still
// paints edge to edge, the page inside clears the system bars. Zero on desktop.
Dialog {
    parent: Overlay.overlay
    modal: true
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    topPadding: parent ? parent.SafeArea.margins.top : 0
    bottomPadding: parent ? parent.SafeArea.margins.bottom : 0
    leftPadding: parent ? parent.SafeArea.margins.left : 0
    rightPadding: parent ? parent.SafeArea.margins.right : 0

    background: Rectangle { color: Theme.background }

    // Opening takes the focus off whatever had it, but on Android a keyboard
    // already up for the chat's composer stays up over a page with nothing to
    // type into - the XML viewer, say. A field on the sheet brings it back
    // when tapped.
    onOpened: Qt.inputMethod.hide() // qmllint disable missing-property

    // WORKAROUND for a Qt bug: a modal popup blocks presses to what is behind
    // it, but not drags to the pointer handlers there, which watch through a
    // passive grab. A sideways drag on the XML viewer reached the message
    // under it as a reply swipe, which focused the composer and raised the
    // keyboard. Drop this once Qt blocks them itself:
    //   https://bugreports.qt.io/browse/QTBUG-87815 (DragHandler, through popups)
    //   https://bugreports.qt.io/browse/QTBUG-89873 (touch handlers, Android)
    //   https://bugreports.qt.io/browse/QTBUG-100104 (DragHandler, modal Dialog)
    // `covers` is the content the sheet is drawn over, taken out of reach
    // while it is up.
    property Item covers: null
    Binding {
        target: covers
        property: "enabled"
        value: false
        when: covers !== null && visible
        restoreMode: Binding.RestoreBindingOrValue
    }
}
