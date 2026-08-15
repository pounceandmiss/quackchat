import QtQuick
import QtQuick.Controls
import Quack

// Slim themed handle in place of the style's grey rail. On touch it is an
// indicator only: hidden at rest, shown while the view moves.
ScrollBar {
    id: bar

    interactive: !Theme.mobile

    readonly property real restOpacity: Theme.mobile ? 0 : 0.25

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: Theme.textDim
        visible: bar.size < 1
        opacity: bar.restOpacity
    }

    // The style has its own active state and fade over contentItem.opacity,
    // which would take the binding above off it. These replace them.
    states: State {
        name: "active"
        when: bar.size < 1 && (bar.active || bar.pressed)
        PropertyChanges {
            bar.contentItem.opacity: bar.pressed ? 0.8 : 0.5
        }
    }

    transitions: Transition {
        from: "active"
        SequentialAnimation {
            PauseAnimation { duration: 450 }
            NumberAnimation {
                target: bar.contentItem
                property: "opacity"
                to: bar.restOpacity
                duration: 200
            }
        }
    }
}
