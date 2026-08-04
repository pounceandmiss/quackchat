pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

Page {
    id: page
    objectName: "chatPane"
    property string chatJid: ""
    property string chatName: ""
    // Room sync carries the room's jid; account sync gets an empty one (1:1 only).
    property bool chatGroupchat: false
    property string account: ""
    property bool showBack: true
    // Off inside a pop-out window, so you cannot pop a pop-out.
    property bool canPopOut: false
    readonly property bool hasChat: chatJid !== ""
    signal back()
    signal popOut()

    // Android overlays the keyboard instead of resizing the window, so the
    // content column has to shift up by this much itself. Zero elsewhere.
    readonly property real keyboardInset: {
        if (!Qt.inputMethod.visible) // qmllint disable missing-property
            return 0
        const kb = Qt.inputMethod.keyboardRectangle // qmllint disable missing-property
        if (kb.height <= 0)
            return 0
        // Overlap between the keyboard and this page, in window coordinates.
        const pageBottom = mapToItem(null, 0, height).y
        return Math.max(0, Math.min(pageBottom - kb.y, height))
    }

    background: Rectangle {
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.background }
            GradientStop { position: 1.0; color: Theme.background2 }
        }
    }

    // Hidden helper that places copied message text on the system clipboard.
    TextEdit { id: clip; visible: false }

    ChatModel {
        id: chatModel
        backend: App.backend
        account: page.account
        chat: page.chatJid
        groupchat: page.chatGroupchat
    }

    // ChatModel has no "selected" role, so selection lives here, keyed by each
    // message's timestamp id. Every change swaps in a fresh object so the `var`
    // binding re-evaluates and delegates re-read isSelected().
    property var selectedBodies: ({})
    readonly property int selectedCount: Object.keys(selectedBodies).length
    readonly property bool selectionMode: selectedCount > 0

    function isSelected(ts) { return selectedBodies[ts] !== undefined }
    function toggle(ts, body) {
        const next = Object.assign({}, selectedBodies)
        if (next[ts] !== undefined)
            delete next[ts]
        else
            next[ts] = body
        selectedBodies = next
    }
    function clearSelection() { selectedBodies = ({}) }
    function copySelected() {
        // Object keys iterate in ascending numeric order, so this joins the
        // chosen messages oldest-first regardless of the tap order.
        copyText(Object.values(selectedBodies).join("\n"))
        clearSelection()
    }
    function copyText(t) { clip.text = t; clip.selectAll(); clip.copy() }

    // Reply composer is GUI only, not threaded through the backend.
    property string replyBody: ""
    property bool replyOutgoing: false
    readonly property bool replying: replyBody !== ""

    function startReply(body, outgoing) {
        replyBody = body
        replyOutgoing = outgoing
        input.forceActiveFocus()
    }
    function cancelReply() { replyBody = ""; replyOutgoing = false }

    // GUI-only dummy, not sent anywhere. Keyed by timestamp so a reaction
    // survives its bubble being recycled as you scroll.
    property var reactions: ({})
    function reactionFor(ts) { return reactions[ts] !== undefined ? reactions[ts] : "" }
    function react(ts, emoji) {
        const next = Object.assign({}, reactions)
        if (next[ts] === emoji)
            delete next[ts]
        else
            next[ts] = emoji
        reactions = next
    }

    function fmtTime(ts) {
        if (!ts) return ""
        const d = new Date(ts / 1000) // tacky timestamps are microseconds
        return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
    }
    function fmtStatus(s) {
        // server_status: "" (server has it) | pending | uploading | failed
        return s === "" ? "read" : "sent"
    }
    function sendCurrent() {
        const t = input.text.trim()
        if (t.length === 0) return
        chatModel.send(t)
        input.clear()
        cancelReply()
    }

    header: Rectangle {
        height: page.hasChat ? 60 : 0
        visible: page.hasChat
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 14
            spacing: 10
            visible: !page.selectionMode
            IconButton {
                text: "‹"
                font.pixelSize: 28
                visible: page.showBack
                onClicked: page.back()
            }
            Item { visible: !page.showBack; Layout.preferredWidth: 4 }
            Avatar {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                account: page.account
                jid: page.chatJid
                label: page.chatName !== "" ? page.chatName : page.chatJid
                initialsPixelSize: 15
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: page.chatName !== "" ? page.chatName : page.chatJid
                    color: Theme.textPrimary
                    font.pixelSize: 16
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: page.chatJid
                    visible: page.chatName !== ""
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            IconButton {
                text: "⧉"
                glyphColor: Theme.textDim
                visible: page.canPopOut && page.hasChat
                onClicked: page.popOut()
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 16
            spacing: 16
            visible: page.selectionMode
            IconButton {
                text: "✕"
                onClicked: page.clearSelection()
            }
            Text {
                Layout.fillWidth: true
                text: page.selectedCount + " selected"
                color: Theme.textPrimary
                font.pixelSize: 17
                font.bold: true
            }
            IconButton {
                text: "⧉"
                font.pixelSize: 20
                glyphColor: Theme.accentDeep
                onClicked: page.copySelected()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: Theme.hairline
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - 60
        visible: !page.hasChat
        spacing: 10
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "🦆"
            font.pixelSize: 44
            opacity: 0.55
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 14
            text: "Pick a conversation to start chatting."
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.bottomMargin: page.keyboardInset
        Behavior on anchors.bottomMargin {
            NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
        }
        spacing: 0
        visible: page.hasChat

        ListView {
            id: feed
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: chatModel
            clip: true
            spacing: 10
            topMargin: 14
            bottomMargin: 8
            verticalLayoutDirection: ListView.BottomToTop
            cacheBuffer: 400
            boundsBehavior: Flickable.StopAtBounds

            // Slim themed handle, faintly visible when overflowing, firming up
            // while scrolling or dragging.
            ScrollBar.vertical: ScrollBar {
                id: feedScroll
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: Theme.textDim
                    visible: feedScroll.size < 1
                    opacity: feedScroll.pressed ? 0.8 : feedScroll.active ? 0.5 : 0.25
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }
            }

            // Scroll-driven paging (onContentYChanged below) only fires once the
            // content is tall enough to scroll, and a cursorless initial load from
            // tacky doesn't reach far enough back to guarantee that. Keep pulling
            // older pages here until the feed can scroll or the archive runs dry.
            // BottomToTop: atYEnd is the oldest edge, so olderBuffer is the
            // distance left to it; under-tall content pins contentY and drives it
            // negative, which is what triggers the fill.
            readonly property real olderBuffer: originY + contentHeight - height - contentY
            readonly property real fillThreshold: Math.max(400, height)
            property bool olderExhausted: false
            property bool topping: false

            function topUp() {
                if (!page.hasChat || olderExhausted || topping || count === 0)
                    return
                if (olderBuffer >= fillThreshold)
                    return
                topping = true
                chatModel.loadOlder()
            }

            Connections {
                target: chatModel
                // A reload (chat/account switch) empties the window; a fresh chat
                // may again be under-tall, so clear the exhausted latch and refill.
                function onChatChanged() { feed.olderExhausted = false; feed.topping = false }
                function onAccountChanged() { feed.olderExhausted = false; feed.topping = false }
                function onLoaded(dir, added) {
                    if (dir !== "init" && dir !== "old")
                        return
                    feed.topping = false
                    // A fresh newest page is a brand-new window: whatever we knew
                    // about the old end no longer holds, so clear the latch.
                    if (dir === "init")
                        feed.olderExhausted = false
                    // An older pull that added nothing means the archive is dry;
                    // stop, so we don't spin re-requesting an empty page.
                    else if (added === 0)
                        feed.olderExhausted = true
                    Qt.callLater(feed.topUp)
                }
            }
            onCountChanged: Qt.callLater(topUp)
            onHeightChanged: Qt.callLater(topUp)

            delegate: Item {
                id: wrap
                required property string body
                required property bool outgoing
                required property string serverStatus
                required property var timestamp
                width: feed.width
                height: bubble.height
                ChatBubble {
                    id: bubble
                    width: feed.width
                    text: wrap.body
                    outgoing: wrap.outgoing
                    time: page.fmtTime(wrap.timestamp)
                    status: page.fmtStatus(wrap.serverStatus)
                    selectionMode: page.selectionMode
                    selected: page.isSelected(wrap.timestamp)
                    reaction: page.reactionFor(wrap.timestamp)
                    onToggleRequested: page.toggle(wrap.timestamp, wrap.body)
                    onCopyRequested: page.copyText(wrap.body)
                    onReplyRequested: page.startReply(wrap.body, wrap.outgoing)
                    onReactRequested: (emoji) => page.react(wrap.timestamp, emoji)
                }
            }

            // BottomToTop: the newest edge is atYBeginning, the oldest atYEnd.
            // The model's load* calls are idempotent (guarded by in-flight tags).
            onContentYChanged: {
                if (atYEnd) chatModel.loadOlder()
                else if (atYBeginning) chatModel.loadNewer()
            }
        }

        Rectangle {
            id: replyBanner
            Layout.fillWidth: true
            Layout.preferredHeight: page.replying ? 52 : 0
            clip: true
            visible: Layout.preferredHeight > 0
            color: Theme.surface
            Behavior on Layout.preferredHeight { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

            Rectangle {
                anchors.top: parent.top
                width: parent.width; height: 1
                color: Theme.hairline
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 10
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 3
                    radius: 1.5
                    color: Theme.accent
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: page.replyOutgoing ? "Reply to You"
                            : "Reply to " + (page.chatName !== "" ? page.chatName : page.chatJid)
                        color: Theme.accent
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: page.replyBody
                        color: Theme.textDim
                        font.pixelSize: 13
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        Layout.fillWidth: true
                    }
                }
                IconButton {
                    text: "✕"
                    font.pixelSize: 16
                    glyphColor: Theme.textDim
                    onClicked: page.cancelReply()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: Theme.surface
            Rectangle {
                anchors.top: parent.top
                width: parent.width; height: 1
                color: Theme.hairline
            }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 20
                    color: Theme.field
                    TextField {
                        id: input
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: "Message"
                        placeholderTextColor: Theme.textDim
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        background: Item {}
                        // Label the virtual keyboard's enter key "Send" (its
                        // press still lands here as accepted).
                        EnterKey.type: Qt.EnterKeySend
                        onAccepted: page.sendCurrent()
                    }
                }
                Rectangle {
                    id: sendBtn
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    radius: 22
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0.0; color: Theme.accent2 }
                        GradientStop { position: 1.0; color: Theme.accent }
                    }
                    opacity: input.text.trim().length > 0 ? 1.0 : 0.5
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    Text { anchors.centerIn: parent; text: "➤"; color: Theme.onAccent; font.pixelSize: 18 }
                    MouseArea { anchors.fill: parent; onClicked: page.sendCurrent() }
                }
            }
        }
    }
}
