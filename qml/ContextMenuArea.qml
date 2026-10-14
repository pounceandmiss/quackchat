import QtQuick
import QtQuick.Controls

// How anything in the app offers its menu: a right-click with a mouse, a long
// press on touch. Either one reports where it happened, in the parent's
// coordinates, so the menu opens under the finger - a bare popup() goes by the
// mouse cursor, which a touch never moves.
//
// Set `menu` and it pops up there by itself; handle `requested` instead when
// the menu wants loading first, or someone else owns it.
//
// In a button (an ItemDelegate row), the long press is the button's own
// pressAndHold: a held press then ends without `clicked`, where this handler's
// would leave the button to click on release as well.
TapHandler {
    id: area

    property Menu menu: null

    signal requested(real x, real y)

    // By its signal: `instanceof AbstractButton` does not match an
    // ItemDelegate here.
    readonly property Item button:
        area.parent && typeof area.parent.pressAndHold === "function" ? area.parent : null

    function request(x, y) {
        if (area.menu)
            area.menu.popup(area.parent, x, y)
        area.requested(x, y)
    }

    // A touch point carries no button, so the right one can only be a mouse's.
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    onTapped: (eventPoint, button) => {
        if (button === Qt.RightButton)
            area.request(eventPoint.position.x, eventPoint.position.y)
    }
    onLongPressed: {
        if (!area.button)
            area.request(area.point.position.x, area.point.position.y)
    }

    property Connections _held: Connections {
        target: area.enabled ? area.button : null
        function onPressAndHold() {
            area.request(area.button.pressX, area.button.pressY)
        }
    }
}
