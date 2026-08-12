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
    // Who sent it, drawn above the body when the chat has more than two voices.
    property string author: ""
    property bool showAuthor: false

    // The message this one answers: a one-line preview and its author, both
    // resolved by tacky. Empty when this is not a reply.
    property string replyBody: ""
    property string replyAuthor: ""
    readonly property bool isReply: replyBody !== ""
    // The message's attachments, each already merged with the state of its
    // transfer by ChatModel. Empty for a plain message.
    property var attachments: []
    // Open what is already on disk; load what is not (a held-back autofetch, a
    // failed fetch, or a file nobody has asked for yet).
    signal attachmentOpenRequested(int idx)
    signal attachmentLoadRequested(int idx)
    property string time: ""
    property bool outgoing: false
    // pending | failed | sent | delivered | read, only drawn for our own
    // messages. See ChatPage.fmtStatus for how the two backend fields fold
    // into these.
    property string status: "sent"

    // Whether this message was handled by OMEMO. Independent of `status`: the
    // padlock says how the message was carried, the tick says how far it got,
    // so a failed encrypted message shows both rather than hiding one. A
    // message tacky could not decrypt is encrypted too - its body is the
    // placeholder, and the padlock is honest about where it came from.
    property bool encrypted: false
    // Whether the chat this message sits in is set to encrypt, which is what
    // decides if an unencrypted one is worth pointing out.
    property bool chatEncrypting: false

    // Both already folded by the page, as `status` is: the bubble draws the
    // choices, it does not work out which ones apply.
    property bool canRetry: false
    property bool canResendPlain: false

    property bool selected: false
    property bool selectionMode: false

    // The aggregated map from the backend: emoji -> {reactors, mine}.
    property var reactions: ({})
    readonly property var reactionKeys: reactions ? Object.keys(reactions) : []
    readonly property var reactionChoices: ["👍", "❤️", "😂", "😮", "😢", "🙏"]
    signal toggleRequested()
    signal copyRequested()
    signal replyRequested()
    signal reactRequested(string emoji)
    signal retryRequested()
    signal resendPlainRequested()
    signal viewXmlRequested()
    // Tapping the quote jumps to the message it previews.
    signal quoteTapped()

    // Briefly tinted after a jump lands on this row.
    property bool highlighted: false

    readonly property real maxBubbleWidth: Math.min(parent ? parent.width * 0.72 : 320, 480)

    // Byte counts as the chip shows them.
    function fmtSize(n) {
        if (!n || n <= 0)
            return ""
        const units = ["B", "KB", "MB", "GB"]
        let v = n
        let i = 0
        while (v >= 1024 && i < units.length - 1) {
            v /= 1024
            i++
        }
        return (i === 0 ? v : v.toFixed(1)) + " " + units[i]
    }

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

    AppMenu {
        id: ctxMenu
        objectName: "bubbleMenu"
        width: 180

        MenuEntry { text: "React";  onTriggered: reactionBar.open() }
        MenuEntry { text: "Reply";  onTriggered: root.replyRequested() }
        MenuEntry { text: "Copy";   onTriggered: root.copyRequested() }
        MenuEntry { text: "Select"; onTriggered: root.toggleRequested() }
        // Only offered on a message that needs them.
        MenuEntry {
            objectName: "retryEntry"
            text: "Retry"
            offered: root.canRetry
            onTriggered: root.retryRequested()
        }
        MenuEntry {
            objectName: "resendPlainEntry"
            text: "Send without encryption"
            // A downgrade to warn about, not a deletion to fear; the tick has
            // the negative colour already.
            labelColor: Theme.warning
            offered: root.canResendPlain
            onTriggered: root.resendPlainRequested()
        }
        // Ungated, unlike the two above: a message with nothing recorded is
        // itself an answer the viewer is there to give.
        MenuEntry {
            objectName: "viewXmlEntry"
            text: "View XML"
            onTriggered: root.viewXmlRequested()
        }
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
                Glyph {
                    anchors.centerIn: parent
                    path: Icons.moreHoriz
                    color: Theme.textDim
                    size: 20
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
        // The jump tint fades in fast and out slowly, so the eye catches where
        // it landed without the row staying marked.
        opacity: root.selected ? 0.45 : root.highlighted ? 0.35 : 0.0
        Behavior on opacity { NumberAnimation { duration: root.highlighted ? 120 : 450 } }
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
        Glyph {
            anchors.centerIn: parent
            path: Icons.check
            color: Theme.textOnAccent
            size: 16
            visible: root.selected
        }
        // Hit area padded well past the 24px visual for touch.
        MouseArea { anchors.fill: parent; anchors.margins: -10; onClicked: root.toggleRequested() }
    }

    Glyph {
        id: replyHint
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: 14
        path: Icons.reply
        size: 22
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
            objectName: "bubbleBody"
            // A message in the clear is only worth remarking on while the chat
            // is set to encrypt - the same message in a chat with encryption
            // turned off is exactly what was asked for, and looks ordinary.
            readonly property color base: root.outgoing ? Theme.bubbleOut : Theme.bubbleIn
            readonly property color exposed: Qt.tint(bubble.base,
                                                     Qt.rgba(Theme.warning.r, Theme.warning.g,
                                                             Theme.warning.b, 0.55))
            readonly property bool flagged: !root.encrypted && root.chatEncrypting
            readonly property color toColor: bubble.flagged ? bubble.exposed : bubble.base
            readonly property color fromColor: bubble.flagged ? bubble.base : bubble.exposed

            // 0 while the new colour is still crossing, 1 once it has arrived.
            property real sweep: 1
            onFlaggedChanged: sweepAnim.restart()
            NumberAnimation {
                id: sweepAnim
                target: bubble
                property: "sweep"
                from: 0
                to: 1
                duration: 420
                easing.type: Easing.InOutCubic
            }

            // A flat fill either side of the change. The gradient belongs to
            // the wash below and goes with it - left on the bubble it would
            // settle as a permanent two-tone ramp rather than a colour.
            color: sweepAnim.running ? bubble.fromColor : bubble.toColor
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

            // The new colour crossing the bubble from the left, over the old
            // one underneath. Only around while it is moving, so what is left
            // afterwards is the flat fill and not this. Its trailing edge fades
            // by alpha alone - fading towards a colour would tint the middle of
            // the sweep with whatever that colour was.
            Rectangle {
                id: wash
                objectName: "bubbleWash"
                anchors.fill: parent
                radius: parent.radius
                visible: sweepAnim.running
                readonly property color faded: Qt.rgba(bubble.toColor.r, bubble.toColor.g,
                                                       bubble.toColor.b, 0)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: bubble.toColor }
                    GradientStop { position: bubble.sweep; color: bubble.toColor }
                    GradientStop { position: Math.min(1, bubble.sweep + 0.18); color: wash.faded }
                    GradientStop { position: 1; color: wash.faded }
                }
            }

            // Little tail on the bottom corner
            Canvas {
                id: tail
                objectName: "bubbleTail"
                width: 12; height: 14
                anchors.bottom: parent.bottom
                anchors.right: root.outgoing ? parent.right : undefined
                anchors.left:  root.outgoing ? undefined : parent.left
                anchors.rightMargin: root.outgoing ? -5 : 0
                anchors.leftMargin:  root.outgoing ? 0 : -5
                // Crossfaded over the same span as the wash rather than taking
                // the bubble's settled colour, which would snap at the end
                // while the fill beside it was still moving.
                property color fill: bubble.toColor
                Behavior on fill { ColorAnimation { duration: 420 } }
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

                Text {
                    objectName: "authorLine"
                    visible: root.showAuthor && root.author !== ""
                    text: root.author
                    color: Theme.accent
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.maximumWidth: root.maxBubbleWidth
                }

                // The quoted target, above the answer. tacky keeps the outgoing
                // body free of the "> " wire fallback, so nothing is repeated
                // between this and the text below.
                RowLayout {
                    id: replyQuote
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

                    // Above the bubble's own handlers, so a tap on the quote
                    // jumps rather than opening the reaction bar.
                    TapHandler {
                        enabled: !root.selectionMode
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.quoteTapped()
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                // The attachments, above the caption. An image shows the
                // thumbnail tacky derived for it; everything else - a plain
                // file, an image still coming, one the autofetch policy held
                // back - shows a chip naming it.
                Repeater {
                    model: root.attachments
                    delegate: ColumnLayout {
                        id: att
                        required property int index
                        required property var modelData

                        // What is on disk is what these read, so the neutral
                        // ends - never fetched, and the `idle` tacky reports
                        // for one it held back, capped or cancelled - need no
                        // case of their own. Only `failed` has something extra
                        // to say.
                        readonly property bool isImage: att.modelData.type === "image"
                        readonly property bool hasThumb: att.modelData.thumburl != ""
                        readonly property bool busy: att.modelData.state === "active"
                        readonly property bool failed: att.modelData.state === "failed"
                        readonly property string hint: {
                            if (att.busy)
                                return "Downloading…"
                            if (att.failed)
                                return att.modelData.error
                            if (att.isImage && !att.hasThumb)
                                return "Tap to load"
                            // tacky knows an outgoing file's size up front; an
                            // incoming one's only arrives as the transfer's
                            // Content-Length, so either may be the known one.
                            return root.fmtSize(att.modelData.size > 0
                                                ? att.modelData.size
                                                : att.modelData.total)
                        }

                        // Same rule as the Tk client's: a shown thumbnail means
                        // the file is on disk, so a tap opens it; anything else
                        // has to be fetched first.
                        function activate() {
                            if (att.failed || (att.isImage && !att.hasThumb))
                                root.attachmentLoadRequested(att.index)
                            else
                                root.attachmentOpenRequested(att.index)
                        }

                        spacing: 3
                        Layout.maximumWidth: root.maxBubbleWidth
                        Layout.bottomMargin: 3

                        Image {
                            id: thumb
                            objectName: "attachmentThumb"
                            visible: att.isImage && att.hasThumb
                            // The thumbnail path is derived from the URL alone,
                            // so a re-fetched image reuses it - a cached pixmap
                            // would keep showing the old one.
                            cache: false
                            source: att.modelData.thumburl
                            fillMode: Image.PreserveAspectFit
                            readonly property real drawWidth:
                                Math.min(thumb.implicitWidth, root.maxBubbleWidth)
                            Layout.preferredWidth: thumb.drawWidth
                            Layout.preferredHeight: thumb.implicitWidth > 0
                                ? thumb.drawWidth * thumb.implicitHeight / thumb.implicitWidth
                                : 0
                            TapHandler {
                                enabled: !root.selectionMode
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: att.activate()
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }

                        Rectangle {
                            id: attChip
                            objectName: "attachmentChip"
                            visible: !thumb.visible
                            Layout.preferredWidth: Math.min(attChipRow.implicitWidth + 20,
                                                            root.maxBubbleWidth)
                            Layout.preferredHeight: 44
                            radius: 10
                            color: Theme.field

                            RowLayout {
                                id: attChipRow
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Glyph {
                                    path: att.isImage ? Icons.image : Icons.attachFile
                                    color: Theme.textDim
                                    size: 18
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text {
                                        Layout.fillWidth: true
                                        // Never the url: an aesgcm:// fragment
                                        // carries the media key.
                                        text: att.modelData.name
                                        color: Theme.textPrimary
                                        font.pixelSize: 13
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: att.hint !== ""
                                        text: att.hint
                                        color: att.failed ? Theme.negative : Theme.textDim
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            TapHandler {
                                enabled: !root.selectionMode
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: att.activate()
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }

                        // Under whichever of the two is showing, so an image
                        // being re-fetched keeps its old thumbnail meanwhile.
                        Rectangle {
                            objectName: "attachmentProgress"
                            visible: att.busy && att.modelData.total > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            radius: 1.5
                            color: Theme.hairline
                            Rectangle {
                                width: parent.width * Math.min(1,
                                    att.modelData.loaded / att.modelData.total)
                                height: parent.height
                                radius: parent.radius
                                color: Theme.accent
                            }
                        }
                    }
                }

                TextEdit {
                    id: bodyText
                    objectName: "bubbleText"
                    readonly property bool rich: root.markup !== ""
                    text: bodyText.rich ? root.markup : root.text
                    textFormat: bodyText.rich ? TextEdit.RichText : TextEdit.PlainText
                    // A bare share has no caption (tacky blanks a body that is
                    // just the url), and an empty line under the image reads as
                    // a gap in the bubble.
                    visible: text !== ""
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
                    // Ahead of the time, so it keeps its place on an incoming
                    // message, where the tick beside it is not drawn. Bigger
                    // than the text around it because the colour form of the
                    // glyph is detailed enough to smudge at footnote size.
                    Text {
                        objectName: "lockBadge"
                        Layout.alignment: Qt.AlignVCenter
                        text: root.encrypted ? "🔒" : "🔓"
                        font.pixelSize: 16
                    }
                    Text {
                        text: root.time
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Glyph {
                        objectName: "statusTick"
                        visible: root.outgoing
                        path: root.status === "pending" ? Icons.schedule
                            : root.status === "failed" ? Icons.close
                            : root.status === "sent" ? Icons.check : Icons.doneAll
                        color: root.status === "failed" ? Theme.negative
                             : root.status === "read" ? Theme.positive : Theme.textDim
                        size: 13
                    }
                }
            }
        }

        // One chip per emoji, each carrying its count, with our own picks
        // outlined. Tapping a chip toggles that emoji, same call as picking it
        // from the bar.
        Row {
            id: reactionRow
            objectName: "reactionRow"
            visible: root.reactionKeys.length > 0
            anchors.top: bubble.bottom
            anchors.topMargin: -11
            anchors.right: root.outgoing ? bubble.right : undefined
            anchors.left:  root.outgoing ? undefined : bubble.left
            anchors.rightMargin: 10
            anchors.leftMargin: 10
            spacing: 3
            z: 5

            Repeater {
                model: root.reactionKeys
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    readonly property var entry: root.reactions[chip.modelData]
                    readonly property int count: entry && entry.reactors ? entry.reactors.length : 0
                    readonly property bool mine: entry ? entry.mine === true : false

                    height: 22
                    width: chipRow.width + 12
                    radius: 11
                    color: Theme.surface
                    border.color: chip.mine ? Theme.accent : Theme.hairline
                    border.width: chip.mine ? 1.5 : 1

                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 3
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: chip.modelData
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: chip.count > 1
                            text: chip.count
                            color: chip.mine ? Theme.accent : Theme.textDim
                            font.pixelSize: 11
                            font.bold: true
                        }
                    }
                    TapHandler { onTapped: root.reactRequested(chip.modelData) }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
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
