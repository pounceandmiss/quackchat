import QtQuick
import QtQuick.Controls
import Quack

// What every one of this app's windows carries: the palette, the shortcut that
// cycles it, and a content area held clear of notches and system bars.
//
// Children land in that area rather than in the window itself. Theme.background
// still paints behind the bars, which is what keeps them looking like part of
// the app. All zeros on desktop.
ApplicationWindow {
    id: win

    default property alias content: safe.data
    // For the few things that want the window's own edges and account for the
    // insets themselves, as BackendNotice does with the top one.
    property alias edgeToEdge: edge.data

    visible: true
    color: Theme.background

    contentData: [
        Item {
            id: safe
            anchors.fill: parent
            anchors.topMargin: SafeArea.margins.top
            anchors.bottomMargin: SafeArea.margins.bottom
            anchors.leftMargin: SafeArea.margins.left
            anchors.rightMargin: SafeArea.margins.right
        },
        // After the safe area, so what goes here floats over the page rather
        // than under it.
        Item {
            id: edge
            anchors.fill: parent
        },
        Shortcut {
            sequence: "Ctrl+T"
            onActivated: Theme.cycle()
        }
    ]
}
