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
}
