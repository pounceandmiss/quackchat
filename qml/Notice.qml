pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One card in a window's notice stack (see NoticeHost). Swiped sideways or up
// it goes, and so it can sit over the page without ever being in the way: a
// header it covers is a flick from being back. A tap does nothing, so a tap
// meant for what is underneath, landing a moment after the card came up,
// can't throw away what it says unread.
//
// A Pane rather than a Rectangle: a Pane takes the clicks and touches that
// land on it, where a Rectangle lets them through to the header it covers.
Pane {
    id: card

    property string text: ""
    // "warning" for a wait, "negative" for a failure.
    property string tone: "warning"
    property bool copyable: false
    // What Copy takes, when the card says more than is worth pasting.
    property string copyText: card.text
    property string actionText: ""

    signal action
    signal copied
    // Swiped or closed. The host takes it from there.
    signal dismissed

    // A card being read, or about to be swiped, doesn't time out under the
    // finger (see NoticeHost).
    readonly property bool held: swipe.active || longPress.pressed || hover.hovered

    readonly property color toneColor: card.tone === "negative" ? Theme.negative
                                                                : Theme.warning
    property real shiftX: 0
    property real shiftY: 0

    topPadding: 8
    bottomPadding: 8
    leftPadding: 12
    rightPadding: 4
    background: Rectangle {
        radius: 8
        color: Theme.surface
        border.width: 1
        border.color: card.toneColor
    }
    // Fades as it goes, so a half-swiped card already reads as on its way out.
    opacity: 1 - 0.7 * Math.min(1, Math.max(Math.abs(card.shiftX) / Math.max(card.width, 1),
                                            -card.shiftY / Math.max(card.height, 1)))
    transform: Translate { x: card.shiftX; y: card.shiftY }

    function copy() {
        Clipboard.setText(card.copyText)
        card.copied()
    }

    // Off in the direction it was swiped: sideways if it went more sideways
    // than up, else up.
    function fling() {
        if (Math.abs(card.shiftX) >= -card.shiftY) {
            awayX.to = (card.shiftX < 0 ? -1 : 1) * (card.width + 40)
            awayY.to = card.shiftY
        } else {
            awayX.to = card.shiftX
            awayY.to = -(card.height + 40)
        }
        away.start()
    }

    ParallelAnimation {
        id: away
        NumberAnimation { id: awayX; target: card; property: "shiftX"; duration: 160 }
        NumberAnimation { id: awayY; target: card; property: "shiftY"; duration: 160 }
        onFinished: card.dismissed()
    }
    ParallelAnimation {
        id: back
        NumberAnimation { target: card; property: "shiftX"; to: 0; duration: 160; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "shiftY"; to: 0; duration: 160; easing.type: Easing.OutCubic }
    }

    // Every device, the mouse included: the close button is the desktop's
    // obvious way, but a drag shouldn't do nothing there either. Upwards only,
    // since down is the page.
    DragHandler {
        id: swipe
        target: null
        onActiveTranslationChanged: if (swipe.active) {
            card.shiftX = swipe.activeTranslation.x
            card.shiftY = Math.min(0, swipe.activeTranslation.y)
        }
        onActiveChanged: {
            if (swipe.active) {
                back.stop()
                return
            }
            if (Math.abs(card.shiftX) > card.width / 3 || card.shiftY < -card.height / 2)
                card.fling()
            else
                back.start()
        }
    }
    TapHandler {
        id: longPress
        enabled: card.copyable
        onLongPressed: card.copy()
    }
    HoverHandler { id: hover }

    contentItem: RowLayout {
        spacing: 4

        Text {
            objectName: "noticeText"
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            text: card.text
            color: Theme.textPrimary
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Button {
            objectName: "noticeCopy"
            visible: card.copyable
            flat: true
            text: qsTr("Copy")
            onClicked: card.copy()
        }
        Button {
            objectName: "noticeAction"
            visible: card.actionText !== ""
            flat: true
            text: card.actionText
            onClicked: card.action()
        }
        IconButton {
            objectName: "noticeClose"
            visible: !Theme.mobile
            Layout.alignment: Qt.AlignTop
            iconPath: Icons.close
            iconSize: 14
            glyphColor: Theme.textDim
            Accessible.name: qsTr("Dismiss")
            onClicked: card.dismissed()
        }
    }
}
