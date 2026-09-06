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

    // Whatever the app has not drawn itself - a text field, a menu, a check box
    // - is drawn by the style from this palette rather than from Theme, and
    // left at the system's that is a GNOME-coloured search entry sitting in a
    // duck-coloured app. Set here because a palette propagates down the item
    // tree, so one block covers every window and every control in it.
    //
    // Roles, not delegates: Fusion (the desktop style, see main.cpp) builds a
    // field's fill, its outline and its focus ring out of these, so the style
    // keeps doing its own drawing. Material, which Android uses, paints from
    // its attached properties instead and ignores all of this.
    palette {
        base: Theme.field
        text: Theme.textPrimary
        placeholderText: Theme.textDim
        // Panels and menus. Fusion also darkens it into the outline it draws
        // round a field, which is why that outline can only ever be a shade of
        // the surface behind it and never as faint as Theme.hairline.
        window: Theme.surface
        windowText: Theme.textPrimary
        button: Theme.field
        buttonText: Theme.textPrimary
        // Selected text, and the ring Fusion derives for the focused field.
        highlight: Theme.accent
        highlightedText: Theme.textOnAccent
        // A group leaves unset roles to the one above, so this is only what a
        // disabled control should say differently: the search entry is disabled
        // until there is an account, and would otherwise read as live.
        disabled {
            text: Theme.textDim
            windowText: Theme.textDim
            buttonText: Theme.textDim
        }
    }

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
