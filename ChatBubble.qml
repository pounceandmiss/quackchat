pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// A single chat message bubble with multi-select support.
Item {
    id: root

    property string text: ""
    // The body as rich text, empty when it has no formatting spans - which
    // keeps the ordinary message off the rich-text path entirely.
    property string markup: ""
    // The message this one answers: a one-line preview and its author, both
    // resolved by tacky. Empty when this is not a reply.
    property string replyBody: ""
    property string replyAuthor: ""
    readonly property bool isReply: replyBody !== ""
    property string time: ""
    property bool outgoing: false
    // "sent" (one tick) or "read" (two), only shown for outgoing messages
    property string status: "read"

    property bool selected: false
    property bool selectionMode: false

    // GUI-only dummy reaction; "" = none.
    property string reaction: ""
    readonly property var reactionChoices: ["👍", "❤️", "😂", "😮", "😢", "🙏"]
    onReactionChanged: if (reaction !== "") reactionPop.restart()
    signal toggleRequested()
    signal copyRequested()
    signal replyRequested()
    signal reactRequested(string emoji)

    readonly property real maxBubbleWidth: Math.min(parent ? parent.width * 0.72 : 320, 480)

    // How far the whole row slides right to make room for the checkbox
    property real selShift: selectionMode ? 40 : 0
    Behavior on selShift { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    // Snaps back (animated) on release unless the swipe passed the commit
    // threshold, which fires replyRequested.
    property real replyPull: 0
    readonly property real replyCommit: 48
    Behavior on replyPull {
        enabled: !swipe.active   // 1:1 with the finger; animate only the snap-back
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    // Right for the selection gutter, left for an in-progress reply swipe.
    readonly property real rowShift: selShift - replyPull

    // x-only so it never competes with vertical list scrolling, and it waits
    // for the drag threshold so a plain tap still opens the menu.
    DragHandler {
        id: swipe
        enabled: !root.selectionMode
        // Touch only, leaving the mouse drag to the body's text selection.
        // Device rather than Qt.platform.os, so a touchscreen laptop gets both.
        acceptedDevices: PointerDevice.TouchScreen
        target: null   // we move rowContent ourselves
        yAxis.enabled: false
        xAxis.enabled: true
        onActiveTranslationChanged: if (active)
            root.replyPull = Math.max(0, Math.min(-activeTranslation.x, root.replyCommit + 28))
        onActiveChanged: if (!active) {
            if (root.replyPull >= root.replyCommit) root.replyRequested()
            root.replyPull = 0
        }
    }

    // Off while selecting (that mode owns taps for the checkboxes).
    TapHandler {
        enabled: !root.selectionMode
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: root.reactRequested("❤️")
        onLongPressed: reactionBar.open()
    }

    implicitWidth: parent ? parent.width : rowContent.width
    implicitHeight: rowContent.height

    Menu {
        id: ctxMenu
        objectName: "bubbleMenu"
        width: 180
        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.hairline
        }

        component MenuEntry: MenuItem {
            id: mi
            height: 40
            property color labelColor: Theme.textPrimary
            contentItem: Text {
                text: mi.text
                color: mi.labelColor
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            background: Rectangle {
                color: mi.highlighted ? Theme.menuHover : "transparent"
                radius: 6
            }
        }

        MenuEntry { text: "React";  onTriggered: reactionBar.open() }
        MenuEntry { text: "Reply";  onTriggered: root.replyRequested() }
        MenuEntry { text: "Copy";   onTriggered: root.copyRequested() }
        MenuEntry { text: "Select"; onTriggered: root.toggleRequested() }
    }

    // A Popup so it floats in the window overlay above the bubble, never
    // clipped by the list.
    Popup {
        id: reactionBar
        y: -height - 8
        x: root.outgoing ? root.width - width - 16 : 16
        padding: 6
        modal: false
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnReleaseOutside | Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.surface
            radius: height / 2
            border.color: Theme.hairline
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 110 }
            NumberAnimation { property: "scale"; from: 0.7; to: 1; duration: 150; easing.type: Easing.OutBack }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
        }
        contentItem: Row {
            spacing: 2
            Repeater {
                model: root.reactionChoices
                delegate: Rectangle {
                    id: choice
                    required property string modelData
                    width: 40; height: 40; radius: 20
                    color: choiceHover.hovered ? Theme.menuHover : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: choice.modelData
                        font.pixelSize: 22
                        scale: choiceHover.hovered ? 1.3 : 1.0
                        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutBack } }
                    }
                    HoverHandler { id: choiceHover }
                    TapHandler { onTapped: { root.reactRequested(choice.modelData); reactionBar.close() } }
                }
            }
            // Touch path to the actions that sit on right-click for a mouse.
            Rectangle {
                id: moreBtn
                width: 40; height: 40; radius: 20
                color: moreHover.hovered ? Theme.menuHover : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "⋯"
                    color: Theme.textDim
                    font.pixelSize: 20
                }
                HoverHandler { id: moreHover }
                TapHandler {
                    onTapped: {
                        reactionBar.close()
                        ctxMenu.popup(root, reactionBar.x, 0)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: -4
        anchors.bottomMargin: -4
        color: Theme.selection
        opacity: root.selected ? 0.45 : 0.0
        Behavior on opacity { NumberAnimation { duration: 120 } }
    }

    Rectangle {
        id: checkbox
        width: 24; height: 24; radius: 12
        anchors.verticalCenter: parent.verticalCenter
        x: (root.selShift - width) / 2
        visible: root.selectionMode
        color: root.selected ? Theme.accent : "transparent"
        border.color: root.selected ? Theme.accent : Theme.textDim
        border.width: 2
        Text {
            anchors.centerIn: parent
            text: "✓"
            color: Theme.onAccent
            font.pixelSize: 14
            font.bold: true
            visible: root.selected
        }
        // Hit area padded well past the 24px visual for touch.
        MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: root.toggleRequested() }
    }

    Text {
        id: replyHint
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 14
        text: "↩"
        font.pixelSize: 22
        visible: root.replyPull > 0
        opacity: Math.min(1, root.replyPull / root.replyCommit)
        scale: 0.6 + 0.4 * opacity
        color: root.replyPull >= root.replyCommit ? Theme.accent : Theme.textDim
    }

    Item {
        id: rowContent
        x: root.rowShift
        width: parent.width - root.selShift
        height: bubble.height

        Rectangle {
            id: bubble
            color: root.outgoing ? Theme.bubbleOut : Theme.bubbleIn
            radius: 14
            anchors.right: root.outgoing ? parent.right : undefined
            anchors.left:  root.outgoing ? undefined : parent.left
            anchors.rightMargin: root.outgoing ? 10 : 0
            anchors.leftMargin:  root.outgoing ? 0 : 10

            width: content.width + 24
            height: content.height + 16

            Rectangle {
                anchors.fill: parent
                anchors.topMargin: 1
                radius: parent.radius
                color: "#000000"
                opacity: 0.06
                z: -1
            }

            // Little tail on the bottom corner
            Canvas {
                width: 12; height: 14
                anchors.bottom: parent.bottom
                anchors.right: root.outgoing ? parent.right : undefined
                anchors.left:  root.outgoing ? undefined : parent.left
                anchors.rightMargin: root.outgoing ? -5 : 0
                anchors.leftMargin:  root.outgoing ? 0 : -5
                // Repaint when the bubble color changes (theme swap)
                property color fill: root.outgoing ? Theme.bubbleOut : Theme.bubbleIn
                onFillChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.fillStyle = fill;
                    ctx.beginPath();
                    if (root.outgoing) {
                        ctx.moveTo(0, 0);
                        ctx.quadraticCurveTo(12, 4, 12, 14);
                        ctx.quadraticCurveTo(4, 10, 0, 8);
                    } else {
                        ctx.moveTo(12, 0);
                        ctx.quadraticCurveTo(0, 4, 0, 14);
                        ctx.quadraticCurveTo(8, 10, 12, 8);
                    }
                    ctx.closePath();
                    ctx.fill();
                }
            }

            ColumnLayout {
                id: content
                spacing: 2
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.topMargin: 8

                // The quoted target, above the answer. tacky keeps the outgoing
                // body free of the "> " wire fallback, so nothing is repeated
                // between this and the text below.
                RowLayout {
                    objectName: "replyQuote"
                    visible: root.isReply
                    spacing: 7
                    Layout.maximumWidth: root.maxBubbleWidth
                    Layout.bottomMargin: 3

                    Rectangle {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 3
                        radius: 1.5
                        color: Theme.accent
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: root.replyAuthor
                            color: Theme.accent
                            font.pixelSize: 12
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.replyBody
                            color: Theme.textDim
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                    }
                }

                TextEdit {
                    id: bodyText
                    objectName: "bubbleText"
                    readonly property bool rich: root.markup !== ""
                    text: bodyText.rich ? root.markup : root.text
                    textFormat: bodyText.rich ? TextEdit.RichText : TextEdit.PlainText
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    // Free to take the mouse drag now that the swipe is touch
                    // only. Persistent so the highlight survives the focus
                    // moving to the composer.
                    selectByMouse: true
                    persistentSelection: true
                    Layout.maximumWidth: root.maxBubbleWidth
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 4
                    Text {
                        text: root.time
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Text {
                        visible: root.outgoing
                        text: root.status === "sent" ? "✓" : "✓✓"
                        color: root.status === "read" ? Theme.positive : Theme.textDim
                        font.pixelSize: 11
                        font.bold: true
                    }
                }
            }
        }

        Rectangle {
            id: reactionChip
            visible: root.reaction !== ""
            anchors.top: bubble.bottom
            anchors.topMargin: -13
            anchors.right: root.outgoing ? bubble.right : undefined
            anchors.left:  root.outgoing ? undefined : bubble.left
            anchors.rightMargin: 10
            anchors.leftMargin: 10
            width: 26; height: 26; radius: 13
            color: Theme.surface
            border.color: Theme.hairline
            z: 5
            Text { anchors.centerIn: parent; text: root.reaction; font.pixelSize: 15 }
            NumberAnimation {
                id: reactionPop
                target: reactionChip; property: "scale"
                from: 0.3; to: 1.0; duration: 220; easing.type: Easing.OutBack
            }
        }
    }

    // RightButton only, so the left button stays free for the swipe DragHandler.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) { ctxMenu.popup(mouse.x, mouse.y) }
    }

    // Disabled outside selection mode, so taps fall through to the handlers above.
    MouseArea {
        anchors.fill: parent
        enabled: root.selectionMode
        acceptedButtons: Qt.LeftButton
        onClicked: root.toggleRequested()
    }
}
