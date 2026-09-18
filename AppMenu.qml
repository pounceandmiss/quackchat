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

    // The native Windows styles draw a menu's drop shadow inside the background
    // item and make room for it with negative insets (-32 on Windows, the
    // config's shadow sizes on FluentWinUI3). Our own background inherits that
    // room and paints it as blank panel around the entries. Fusion is the
    // default there now (main.cpp), but the style is still one environment
    // variable away, and a shadowless panel wants no room either way.
    leftInset: 0
    topInset: 0
    rightInset: 0
    bottomInset: 0

    background: Rectangle {
        color: Theme.surface
        radius: 10
        border.color: Theme.hairline
    }
}
