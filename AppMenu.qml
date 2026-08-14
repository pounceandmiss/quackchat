import QtQuick
import QtQuick.Controls
import Quack

// The panel every menu in the app is drawn on. Its entries are MenuEntry.
Menu {
    // Qt keeps a menu inside the window, which on Android runs edge to edge
    // under the system bars; these pull it in to what can be seen of it.
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
