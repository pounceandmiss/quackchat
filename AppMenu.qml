import QtQuick
import QtQuick.Controls
import Quack

// The panel every menu in the app is drawn on. Its entries are MenuEntry.
Menu {
    // Qt keeps a menu inside the window it pops up in, and on Android that
    // window runs edge to edge underneath the status and navigation bars: a
    // menu opened from a row near the bottom ends up half behind the gesture
    // bar. These pull its bounds in to the part of the window that can be seen,
    // so it moves clear of them instead. The overlay spans the whole window
    // however the menu is placed, which is what makes it safe to read here.
    readonly property Item windowOverlay: Overlay.overlay
    topMargin: windowOverlay ? windowOverlay.SafeArea.margins.top : 0
    bottomMargin: windowOverlay ? windowOverlay.SafeArea.margins.bottom : 0
    leftMargin: windowOverlay ? windowOverlay.SafeArea.margins.left : 0
    rightMargin: windowOverlay ? windowOverlay.SafeArea.margins.right : 0

    background: Rectangle {
        color: Theme.surface
        radius: 10
        border.color: Theme.hairline
    }
}
