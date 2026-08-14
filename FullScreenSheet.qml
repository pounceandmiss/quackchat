import QtQuick
import QtQuick.Controls
import Quack

// A page shown over the whole window, which is how mobile hosts what desktop
// gives a window of its own. The window's own safe-area inset does not reach
// here: a sheet is parented to the overlay, which spans the screen bars and all.
//
// So it pads itself. The background still paints edge to edge, and the page
// inside sits in the strip between the status and navigation bars rather than
// under them. The margins are read off the overlay rather than off the page,
// which would be reading back the inset it had just applied. All zeros on
// desktop.
Dialog {
    id: sheet

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
