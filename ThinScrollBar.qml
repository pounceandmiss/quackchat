import QtQuick
import QtQuick.Controls
import Quack

// Slim themed handle in place of the style's grey rail. On touch it is an
// indicator only: hidden at rest, shown while the view moves.
ScrollBar {
    id: bar

    interactive: !Theme.mobile

    readonly property real restOpacity: Theme.mobile ? 0 : 0.25

    // A Flickable places the bar attached to it; a ScrollView leaves that to
    // the style's own bar, so in one this does it, or it sits at the view's
    // top-left as a dot. A Binding's value is evaluated even while its `when`
    // is false, hence the null checks.
    readonly property ScrollView view: bar.parent as ScrollView
    Binding on x {
        when: bar.view !== null
        value: !bar.view ? 0
            : bar.vertical ? (bar.view.mirrored ? 0 : bar.view.width - bar.width)
            : bar.view.leftPadding
    }
    Binding on y {
        when: bar.view !== null
        value: !bar.view ? 0
            : bar.vertical ? bar.view.topPadding : bar.view.height - bar.height
    }
    Binding on height {
        when: bar.view !== null && bar.vertical
        value: bar.view?.availableHeight ?? 0
    }
    Binding on width {
        when: bar.view !== null && bar.horizontal
        value: bar.view?.availableWidth ?? 0
    }

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
