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

    // The sender's avatar, drawn in a gutter on the bubble's own side. An empty
    // jid is how the preference turns them off: no face, and no gutter either.
    property string avatarAccount: ""
    property string avatarJid: ""
    // Only the message at the foot of a run from one sender carries the
    // picture. The rest of the run keep the gutter, so their bubbles line up.
    property bool showAvatar: false

    readonly property real avatarEdge: 28
    readonly property real avatarGutter: avatarJid !== "" ? avatarEdge + 8 : 0

    // The message this one answers: a one-line preview and its author, both
    // resolved by tacky. Empty when this is not a reply.
    property string replyBody: ""
    property string replyAuthor: ""
    readonly property bool isReply: replyBody !== ""
    // The message's attachments, each already merged with the state of its
    // transfer by ChatModel. Empty for a plain message.
    property var attachments: []
    // Open what is already on disk; load what is not (a held-back autofetch, a
    // failed fetch, a file nobody has asked for yet, or a share whose upload
    // did not get out - the page picks which way "again" runs).
    signal attachmentOpenRequested(int idx)
    signal attachmentLoadRequested(int idx)
    signal attachmentSaveRequested(int idx)
    signal attachmentFolderRequested(int idx)
    signal attachmentUncacheRequested(int idx)
    signal attachmentCancelRequested(int idx)
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

    // Both already folded by the page, as `status` is: the bubble draws the
    // choices, it does not work out which ones apply.
    property bool canRetry: false
    property bool canResendPlain: false
    property bool canEdit: false
    property bool canDelete: false

    // Withdrawn by its sender: the row is kept so paging and replies still
    // resolve, but there is no content left to draw. Everything the message
    // used to carry - its reactions, the quote it answered, the padlock - is
    // still on the row, and is deliberately not drawn.
    property bool retracted: false
    // Corrected since it was sent. Says so; the body is already the new one.
    property bool edited: false

    property bool selected: false
    property bool selectionMode: false
    // Whether this message has been handed over to text selection, which only
    // ever happens to one of them at a time. While it is on, the row's own
    // gestures stand down so the drag reaches the body's TextEdit.
    property bool textSelecting: false
    // Whether the words picked out in this chat are this message's. The page
    // decides; going false drops the highlight this row was left holding. No
    // row has them until a drag or a hand-over says so.
    property bool ownsWords: false
    onOwnsWordsChanged: if (!root.ownsWords) bodyText.deselect()

    // Android answers a long press on a live body with its own text
    // selection, handles and Copy popup, and takes that press from whatever
    // handler would have had it. So the body only comes alive there once its
    // message is picked: the first long press has to reach the row. A mouse
    // has no long press to lose, so elsewhere the body is live except while
    // selecting, where a click on it picks the message.
    //
    // Settable, since a test has no Android to run on.
    property bool nativeWords: Qt.platform.os === "android"

    // Whether this bubble is the one showing a menu, and whether any of them
    // is. Both come from the page: only it can see across the rows, and a touch
    // on one message has to know what another one is showing.
    property bool menuOpen: false
    property bool anyMenuOpen: false
    signal menuOpened()
    signal menuClosed()
    signal menuDismissRequested()

    // The aggregated map from the backend: emoji -> {reactors, mine}.
    property var reactions: ({})
    readonly property var reactionKeys: reactions ? Object.keys(reactions) : []
    readonly property var reactionChoices: ["👍", "❤️", "😂", "😮", "😢", "🙏"]

    // emoji -> the count last drawn for it. A Repeater over a JS array rebuilds
    // every delegate when the array changes, so a chip cannot tell news from
    // history by its own creation - the row has to remember.
    property var drawnReactions: ({})
    property bool reactionsDrawn: false

    signal toggleRequested()
    // The long press: the message joins the selection before its menu opens.
    signal selectRequested()
    // Any of the menu's own actions having been taken, which says the selection
    // the long press started was a side effect rather than the point.
    signal actionTaken()
    // Long-pressed again while selecting: hand the words over.
    signal textSelectRequested()
    signal textSelectEnded()
    // The body has words picked out in it, by mouse or by hand-over.
    signal wordsTaken()
    signal copyTextRequested(string text)
    signal copyRequested()
    signal replyRequested()
    signal reactRequested(string emoji)
    signal retryRequested()
    signal resendPlainRequested()
    signal editRequested()
    signal deleteRequested()
    signal viewXmlRequested()
    // Tapping the quote jumps to the message it previews.
    signal quoteTapped()

    // Briefly tinted after a jump lands on this row.
    property bool highlighted: false

    readonly property real maxBubbleWidth:
        Math.min(parent ? parent.width * 0.72 : 320, 480) - avatarGutter

    // Silent until the row has drawn once, or a chat scrolled or reloaded would
    // pop every chip on screen at once.
    function reactionEntrance(emoji, count) {
        const before = root.drawnReactions[emoji]
        root.drawnReactions[emoji] = count
        if (!root.reactionsDrawn)
            return ""
        if (before === undefined)
            return "arrived"
        return count > before ? "grew" : ""
    }

    // Forget a reaction taken back, so coming round again is news a second time.
    onReactionsChanged: {
        const live = root.reactions || ({})
        for (const emoji of Object.keys(root.drawnReactions))
            if (live[emoji] === undefined)
                delete root.drawnReactions[emoji]
    }

    // Children complete first, so the chips this bubble was built with have
    // already recorded themselves as history.
    Component.onCompleted: root.reactionsDrawn = true

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
        enabled: !root.selectionMode && !root.retracted
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

    // Touch only: the mouse has the right button for the menu, and its left one
    // belongs to the body's text selection.
    //
    // WithinBounds rather than the default drag threshold, which would only
    // take a passive grab: the thumbnails, the quote and the reaction chips all
    // sit under this handler, and a passive grab fires alongside theirs instead
    // of losing to them. It also means a flick that started here is cancelled
    // when the list takes the grab, so scrolling never lands on a menu.
    TapHandler {
        id: bubbleTap
        enabled: !root.textSelecting
        acceptedDevices: PointerDevice.TouchScreen
        gesturePolicy: TapHandler.WithinBounds
        // Latched on the press. Qt closes an open menu the moment a press lands
        // outside it, so by the time the tap is recognised - on release - there
        // is no menu left to notice; what the touch meant was settled when it
        // started.
        property bool dismissing: false
        onPressedChanged: if (bubbleTap.pressed) bubbleTap.dismissing = root.anyMenuOpen
        onTapped: root.tapped(bubbleTap.point.position, bubbleTap.dismissing)
        onLongPressed: root.pressed(bubbleTap.point.position, bubbleTap.dismissing)
    }

    implicitWidth: parent ? parent.width : rowContent.width
    implicitHeight: rowContent.height

    // What a touch means on this row, in one place: the body's TextEdit takes
    // the press for itself, so its own handler has to ask the same questions
    // the row's does. `dismissing` is a menu having been up when the touch
    // began, which is all it can mean - Qt closes that menu on the press, but
    // the press carries on to whatever it landed on, and landing on a message
    // used to open that message's menu in the same breath.
    function tapped(pos, dismissing) {
        if (dismissing)
            root.menuDismissRequested()
        else if (root.selectionMode)
            root.toggleRequested()
        else
            root.openMenu(pos, false)
    }

    // The long press selects the message and stops there. Nothing comes up over
    // it: what the message can do is drawn along the header the selection puts
    // up, and the menu, reactions included, is still a tap away. Pressed again
    // once it is selected, it gives up its words instead.
    function pressed(pos, dismissing) {
        if (dismissing)
            root.menuDismissRequested()
        else if (root.selectionMode)
            root.textSelectRequested()
        else
            root.selectRequested()
    }

    // A Menu takes focus when it opens, and on Android the software keyboard
    // follows focus: opening this one over a half-typed message would drop the
    // keyboard. Nothing on the touch path needs the focus, so it opens without
    // it; the mouse path keeps it for arrow keys and Escape.
    //
    // The reaction bar rides along wherever the menu goes, since reactions are
    // picked far more often than anything else here. The menu goes up first: the
    // bar sits on top of wherever it ended up, and Qt settles that on placing it.
    function openMenu(pos, keyboard) {
        ctxMenu.focus = keyboard
        ctxMenu.popup(root, pos.x, pos.y)
        // Not over a tombstone: there is nothing left to react to.
        if (!root.retracted)
            reactionBar.open()
        root.menuOpened()
    }

    function closeActions() {
        ctxMenu.close()
        reactionBar.close()
    }

    // Everything the menu and the reaction bar offer goes out through here, so
    // what it means for the selection a press started is said once rather than
    // per entry.
    function take(sig, arg) {
        sig(arg)
        root.actionTaken()
    }

    // The bar opened alongside the menu, so it closes with it: a press outside
    // leaves nothing of the pair behind. The page hears about it too, since it
    // is the one holding "some message has its menu up".
    Connections {
        target: ctxMenu
        function onClosed() {
            reactionBar.close()
            root.menuClosed()
        }
    }

    // The page's answer to menuDismissRequested comes back as this going false.
    onMenuOpenChanged: if (!root.menuOpen) root.closeActions()

    onTextSelectingChanged: {
        if (root.textSelecting)
            bodyText.selectAll()
        else
            bodyText.deselect()
    }

    AppMenu {
        id: ctxMenu
        objectName: "bubbleMenu"
        width: 180

        // A tombstone answers none of these: there is no body to reply to,
        // copy, or pick words out of. Only View XML survives it.
        MenuEntry {
            objectName: "replyEntry"
            text: qsTr("Reply")
            offered: !root.retracted
            onTriggered: root.take(root.replyRequested)
        }
        // The hand-over floats a Copy pill over the words it picks out. A
        // mouse drag has only this.
        MenuEntry {
            objectName: "copySelectionEntry"
            text: qsTr("Copy selection")
            offered: bodyText.selectedText !== ""
            onTriggered: root.take(root.copyTextRequested, bodyText.selectedText)
        }
        MenuEntry {
            objectName: "copyEntry"
            // Says which of the two it is, but only while both are offered.
            text: bodyText.selectedText !== "" ? qsTr("Copy message") : qsTr("Copy")
            offered: !root.retracted
            onTriggered: root.take(root.copyRequested)
        }
        // The way in for the mouse, which has no long press. Never on a message
        // the press already selected, where it could only undo itself.
        MenuEntry {
            objectName: "selectEntry"
            text: qsTr("Select")
            offered: !root.selected && !root.retracted
            onTriggered: root.toggleRequested()
        }
        MenuEntry {
            objectName: "editEntry"
            text: qsTr("Edit")
            offered: root.canEdit
            onTriggered: root.take(root.editRequested)
        }
        MenuEntry {
            objectName: "deleteEntry"
            text: qsTr("Delete")
            // The one entry here that cannot be taken back.
            labelColor: Theme.negative
            offered: root.canDelete
            onTriggered: root.take(root.deleteRequested)
        }
        // Only offered on a message that needs them.
        MenuEntry {
            objectName: "retryEntry"
            text: qsTr("Retry")
            offered: root.canRetry
            onTriggered: root.take(root.retryRequested)
        }
        MenuEntry {
            objectName: "resendPlainEntry"
            text: qsTr("Send without encryption")
            // A downgrade to warn about, not a deletion to fear; the tick has
            // the negative colour already.
            labelColor: Theme.warning
            offered: root.canResendPlain
            onTriggered: root.take(root.resendPlainRequested)
        }
        // Ungated, unlike the two above: a message with nothing recorded is
        // itself an answer the viewer is there to give.
        MenuEntry {
            objectName: "viewXmlEntry"
            text: qsTr("View XML")
            onTriggered: root.take(root.viewXmlRequested)
        }
    }

    // The Tk client's attachment menu. One per bubble rather than one per
    // attachment: openFor() loads it from whichever was clicked.
    AppMenu {
        id: attMenu
        objectName: "attachmentMenu"
        width: 190

        property int idx: 0
        // Everything but Cancel acts on a finished file.
        property bool busy: false

        function openFor(index, att) {
            attMenu.idx = index
            attMenu.busy = att.state === "active"
            attMenu.popup()
        }

        MenuEntry {
            text: qsTr("Open")
            offered: !attMenu.busy
            onTriggered: root.attachmentOpenRequested(attMenu.idx)
        }
        MenuEntry {
            objectName: "attachmentSaveEntry"
            text: qsTr("Save as…")
            offered: !attMenu.busy
            onTriggered: root.attachmentSaveRequested(attMenu.idx)
        }
        MenuEntry {
            objectName: "attachmentFolderEntry"
            text: qsTr("Show in folder")
            offered: !attMenu.busy
            onTriggered: root.attachmentFolderRequested(attMenu.idx)
        }
        MenuEntry {
            objectName: "attachmentUncacheEntry"
            text: qsTr("Delete from cache")
            offered: !attMenu.busy
            onTriggered: root.attachmentUncacheRequested(attMenu.idx)
        }
        MenuEntry {
            objectName: "attachmentCancelEntry"
            text: qsTr("Cancel")
            offered: attMenu.busy
            onTriggered: root.attachmentCancelRequested(attMenu.idx)
        }
    }

    // A Popup so it floats in the window overlay above the bubble, never
    // clipped by the list.
    Popup {
        id: reactionBar
        objectName: "reactionBar"
        // Sat on top of the menu it opens with, sharing its left edge, so the
        // two read as one card. The menu is popped up over the row, so its x and
        // y are already in the row's coordinates, which are this one's too.
        x: ctxMenu.x
        y: ctxMenu.y - height - 8
        padding: 6
        // Opening with every menu, it meets the top of the window often, and a
        // bar off the top edge is one nobody can reach.
        margins: 8
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
                        objectName: "reactionChoice"
                        anchors.centerIn: parent
                        text: choice.modelData
                        // Emoji are bitmap glyphs: item scale magnifies the
                        // raster cached at the resting size, so grow the font.
                        property real grow: choiceHover.hovered ? 1.3 : 1.0
                        font.pixelSize: Math.round(22 * grow)
                        Behavior on grow { NumberAnimation { duration: 90; easing.type: Easing.OutBack } }
                    }
                    HoverHandler { id: choiceHover }
                    TapHandler {
                        onTapped: {
                            root.closeActions()
                            root.take(root.reactRequested, choice.modelData)
                        }
                    }
                }
            }
        }
    }

    // The words are what is selected now, so the way to take them sits with
    // them rather than in the header, which is still counting whole messages.
    // NoAutoClose because the drag that extends the selection lands outside it.
    Popup {
        id: textTools
        objectName: "textTools"
        visible: root.textSelecting
        closePolicy: Popup.NoAutoClose
        margins: 8
        padding: 4
        y: -height - 6
        x: root.outgoing ? root.width - width - 16 : 16
        background: Rectangle {
            color: Theme.surface
            radius: height / 2
            border.color: Theme.hairline
        }
        contentItem: Row {
            spacing: 2
            Rectangle {
                objectName: "copyTextButton"
                // Nothing to take between the press that cleared the selection
                // and the drag that makes a new one.
                visible: bodyText.selectedText !== ""
                width: copyLabel.width + 24
                height: 32
                radius: 16
                color: copyHover.hovered ? Theme.menuHover : "transparent"
                Text {
                    id: copyLabel
                    anchors.centerIn: parent
                    text: qsTr("Copy")
                    color: Theme.accentDeep
                    font.pixelSize: 14
                    font.bold: true
                }
                HoverHandler { id: copyHover }
                TapHandler { onTapped: root.copyTextRequested(bodyText.selectedText) }
            }
            Rectangle {
                objectName: "doneTextButton"
                width: doneLabel.width + 24
                height: 32
                radius: 16
                color: doneHover.hovered ? Theme.menuHover : "transparent"
                Text {
                    id: doneLabel
                    anchors.centerIn: parent
                    text: qsTr("Done")
                    color: Theme.textDim
                    font.pixelSize: 14
                }
                HoverHandler { id: doneHover }
                TapHandler { onTapped: root.textSelectEnded() }
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
            color: root.outgoing ? Theme.bubbleOut : Theme.bubbleIn
            radius: 14
            anchors.right: root.outgoing ? parent.right : undefined
            anchors.left:  root.outgoing ? undefined : parent.left
            anchors.rightMargin: root.outgoing ? root.avatarGutter : 0
            anchors.leftMargin:  root.outgoing ? 0 : root.avatarGutter

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
                    visible: root.isReply && !root.retracted
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

                // The attachments, above the caption. MessageAttachment
                // decides whether a tap on one opens what is on disk or fetches
                // what is not; this wires that answer to the right signal, and
                // hands the message's own menu the attachment it was asked about.
                Repeater {
                    model: root.attachments
                    delegate: MessageAttachment {
                        id: att
                        maxWidth: root.maxBubbleWidth
                        selectionMode: root.selectionMode
                        onOpenRequested: root.attachmentOpenRequested(att.index)
                        onLoadRequested: root.attachmentLoadRequested(att.index)
                        onMenuRequested: attMenu.openFor(att.index, att.modelData)
                    }
                }

                // What the row says instead of its content once it is gone.
                // The header around it stays, so the tombstone still reads as
                // "this person, at this time, said something since withdrawn".
                Text {
                    objectName: "tombstone"
                    visible: root.retracted
                    text: qsTr("This message was deleted")
                    color: Theme.textDim
                    font.pixelSize: 14
                    font.italic: true
                    Layout.maximumWidth: root.maxBubbleWidth
                    wrapMode: Text.Wrap
                }

                TextEdit {
                    id: bodyText
                    objectName: "bubbleText"
                    readonly property bool rich: root.markup !== ""
                    // Keyed on the format, not on `rich`, so the body is
                    // written again after the format moves: on the way out of
                    // rich text Qt fills the plain document with the rich one
                    // serialised, and the row would draw that HTML as its
                    // words until something else touched it.
                    text: bodyText.textFormat === TextEdit.RichText
                        ? root.markup : root.text
                    textFormat: bodyText.rich ? TextEdit.RichText : TextEdit.PlainText
                    // A bare share has no caption (tacky blanks a body that is
                    // just the url), and an empty line under the image reads as
                    // a gap in the bubble.
                    visible: text !== ""
                    color: Theme.textPrimary
                    // Left to the style, the highlight is a colour the theme
                    // never picked.
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    font.pixelSize: 15
                    wrapMode: TextEdit.Wrap
                    readOnly: true
                    // Free to take the mouse drag now that the swipe is touch
                    // only. Persistent so the highlight survives the focus
                    // moving to the composer.
                    selectByMouse: true
                    persistentSelection: true
                    // A click is how a message is picked while selecting, and
                    // the TextEdit would take that press for itself. Disabled
                    // rather than selectByMouse: false, which still swallows it.
                    //
                    // The last term holds a body live until its words are let
                    // go: turned deaf with a selection still on it, Android
                    // leaves the handles behind.
                    enabled: (root.nativeWords
                              ? root.selected || root.textSelecting
                              : root.textSelecting || !root.selectionMode)
                             || bodyText.selectedText !== ""
                    // Fires for the mouse drag and for the hand-over alike.
                    onSelectedTextChanged: if (bodyText.selectedText !== "")
                        root.wordsTaken()
                    Layout.maximumWidth: root.maxBubbleWidth

                    // The TextEdit takes the press for itself, so a touch on the
                    // words never reaches the handlers above. Off only while
                    // this message is selecting text, which is the one time the
                    // TextEdit is meant to have it.
                    TapHandler {
                        id: bodyTap
                        enabled: !root.textSelecting
                        acceptedDevices: PointerDevice.TouchScreen
                        // The grab on press is what takes the press from
                        // Android's selection, so this only watches where
                        // that selection exists; a tap is recognised either
                        // way. Elsewhere the grab is what keeps the press off
                        // the TextEdit, which would take the focus and the
                        // composer's keyboard with it.
                        gesturePolicy: root.nativeWords ? TapHandler.DragThreshold
                                                        : TapHandler.WithinBounds
                        // Under Android's own half second, so a press held
                        // long enough to pick words out has already stood the
                        // release down - it would otherwise tick the message
                        // off from under them.
                        longPressThreshold: 0.4
                        // Latched on the press, for the reason bubbleTap is.
                        property bool dismissing: false
                        onPressedChanged: if (bodyTap.pressed)
                            bodyTap.dismissing = root.anyMenuOpen
                        onTapped: root.tapped(
                            bodyText.mapToItem(root, bodyTap.point.position),
                            bodyTap.dismissing)
                        // Nothing to add where the words answer the press
                        // themselves - but it still has to fire: a handler
                        // that has long-pressed will not call the release
                        // a tap.
                        onLongPressed: if (!root.nativeWords) root.pressed(
                            bodyText.mapToItem(root, bodyTap.point.position),
                            bodyTap.dismissing)
                    }
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
                        // Nothing to badge about a row in the clear - in a room
                        // that is every row. A tombstone keeps the encryption
                        // of the message it replaces, which is no longer
                        // anything to say.
                        visible: root.encrypted && !root.retracted
                        text: "🔒"
                        font.pixelSize: 16
                    }
                    // Beside the time rather than after the words: the body is
                    // a rich-text document built from the markup, and this is
                    // not part of what was said.
                    Text {
                        objectName: "editedMark"
                        visible: root.edited && !root.retracted
                        text: qsTr("edited")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.italic: true
                    }
                    Text {
                        text: root.time
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Glyph {
                        objectName: "statusTick"
                        visible: root.outgoing && !root.retracted
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

        // Level with the foot of the bubble rather than its middle, so a tall
        // message keeps its face on the last line. In rowContent, so it travels
        // with the selection shift and the reply swipe.
        //
        // A Loader rather than a visible: false Avatar: a hidden one still runs
        // its source binding, and that binding is what subscribes the JID.
        Loader {
            objectName: "avatarSlot"
            active: root.showAvatar && root.avatarJid !== ""
            width: root.avatarEdge
            height: root.avatarEdge
            anchors.bottom: bubble.bottom
            anchors.right: root.outgoing ? parent.right : undefined
            anchors.left:  root.outgoing ? undefined : parent.left

            sourceComponent: Avatar {
                objectName: "messageAvatar"
                account: root.avatarAccount
                jid: root.avatarJid
                label: root.author
                initialsPixelSize: 13
            }
        }

        // One chip per emoji, each carrying its count, with our own picks
        // outlined. Tapping a chip toggles that emoji, same call as picking it
        // from the bar.
        Row {
            id: reactionRow
            objectName: "reactionRow"
            visible: root.reactionKeys.length > 0 && !root.retracted
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
                    objectName: "reactionChip"
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

                    Component.onCompleted: {
                        const how = root.reactionEntrance(chip.modelData, chip.count)
                        if (how === "arrived")
                            arrive.start()
                        else if (how === "grew")
                            bump.start()
                    }

                    // Item scale, unlike the picker's hover, which has to grow
                    // the font: this one only passes through the blur on its
                    // way back to 1.0.
                    ParallelAnimation {
                        id: arrive
                        NumberAnimation {
                            target: chip; property: "scale"
                            from: 0.4; to: 1; duration: 190; easing.type: Easing.OutBack
                        }
                        NumberAnimation {
                            target: chip; property: "opacity"
                            from: 0; to: 1; duration: 110
                        }
                    }

                    SequentialAnimation {
                        id: bump
                        NumberAnimation {
                            target: chip; property: "scale"
                            from: 1; to: 1.18; duration: 90; easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            target: chip; property: "scale"
                            to: 1; duration: 130; easing.type: Easing.OutCubic
                        }
                    }

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
                    // Exclusive on press, so the chip wins the tap outright
                    // rather than firing alongside the row's menu.
                    TapHandler {
                        enabled: !root.selectionMode
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: root.reactRequested(chip.modelData)
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }
            }
        }
    }

    // RightButton only, so the left button stays free for the swipe DragHandler.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) { root.openMenu(Qt.point(mouse.x, mouse.y), true) }
    }

    // The mouse's way into the selection the touch long press starts. The mouse
    // alone, and a handler rather than a MouseArea: an enabled MouseArea over
    // the whole row is the frontmost thing under a touch, and takes that touch
    // as a synthesised click before the handlers above are ever offered it.
    TapHandler {
        enabled: root.selectionMode && !root.textSelecting
        acceptedDevices: PointerDevice.Mouse
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.WithinBounds
        onTapped: root.toggleRequested()
    }
}
