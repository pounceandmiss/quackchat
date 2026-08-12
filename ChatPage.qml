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

    // Their keys, hosted the way this app hosts every other page: a window
    // where there are windows, a full-screen sheet where there are not. The
    // page is instantiated from both the shell and a pop-out, so neither of
    // them has to know which.
    function openKeys() {
        if (!page.hasChat || page.chatGroupchat)
            return null
        if (Theme.mobile) {
            keysSheet.open()
            return null
        }
        return AppWindows.omemoKeys(page.account, page.chatJid, page.chatName)
    }

    // Answers whether it had anything to close, so the Android back chain knows
    // whether the press was spent here.
    function closeKeys() {
        if (!keysSheet.opened)
            return false
        keysSheet.close()
        return true
    }

    Dialog {
        id: keysSheet
        objectName: "keysSheet"
        parent: Overlay.overlay
        modal: true
        padding: 0
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0

        OmemoKeysPage {
            anchors.fill: parent
            account: page.account
            jid: page.chatJid
            name: page.chatName
            onDone: keysSheet.close()
        }
    }

    // One message's stanza, hosted the two ways openKeys hosts the keys page.
    // The text is handed over rather than bound, so the viewer keeps showing
    // the row as it stood when it was asked for.
    function viewXml(ts) {
        const xml = chatModel.rawXml(ts)
        if (Theme.mobile) {
            xmlSheet.xml = xml
            xmlSheet.open()
            return null
        }
        return AppWindows.messageXml(xml)
    }

    // Same contract as closeKeys.
    function closeXml() {
        if (!xmlSheet.opened)
            return false
        xmlSheet.close()
        return true
    }

    Dialog {
        id: xmlSheet
        objectName: "xmlSheet"
        parent: Overlay.overlay
        modal: true
        padding: 0
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0

        // "" for a message that never had a stanza built.
        property alias xml: xmlPage.xml

        MessageXmlPage {
            id: xmlPage
            anchors.fill: parent
            onDone: xmlSheet.close()
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
        // The palette's literals, since the markup wants CSS colors and the
        // typed accessors would hand over QColors.
        quoteColor: Theme.p.quote
        matchColor: Theme.p.selection
    }

    OmemoChat {
        id: omemo
        backend: App.backend
        account: page.account
        jid: page.chatJid
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

    // The message being answered. Its timestamp is what the backend needs;
    // the body and direction are only here to draw the composer banner.
    property real replyTo: 0
    property string replyBody: ""
    property bool replyOutgoing: false
    readonly property bool replying: replyTo !== 0

    function startReply(ts, body, outgoing) {
        replyTo = ts
        replyBody = body
        replyOutgoing = outgoing
        input.forceActiveFocus()
    }
    function cancelReply() { replyTo = 0; replyBody = ""; replyOutgoing = false }

    // The row a jump landed on, tinted until the timer below clears it.
    property real highlightTs: 0
    Timer {
        id: highlightFade
        interval: 1600
        onTriggered: page.highlightTs = 0
    }

    // Reading is "this chat is on screen, the app is in front, and the newest
    // message is in view". Anything looser marks a backgrounded window's chat
    // read and swallows its notification.
    readonly property bool reading: page.hasChat && visible
                                    && Qt.application.state === Qt.ApplicationActive // qmllint disable missing-property
                                    && chatModel.atTail
    onReadingChanged: if (reading) chatModel.markRead()

    Connections {
        target: chatModel
        // Every new row while the chat is being read moves the watermark; tacky
        // holds its alert briefly so this lands first and nothing fires.
        function onRowsInserted() {
            if (page.reading)
                chatModel.markRead()
        }

        // A resolved jump either kept the window and scrolled, or replaced it;
        // either way the row exists by now. callLater lets the view lay the
        // new slice out before we ask for its index.
        function onAnchored(ts) {
            if (ts === 0)
                return
            page.highlightTs = ts
            highlightFade.restart()
            Qt.callLater(page.scrollToHighlight)
        }
        // The model resolved an attachment to a file on disk; handing it to the
        // desktop is the one part of opening it that has to happen up here.
        function onAttachmentResolved(url) {
            if (url != "")
                Qt.openUrlExternally(url)
        }
    }
    // Land on a message this window may never have shown, carrying tacky's own
    // content.matches so the row can mark which characters were found. `local`
    // because the target is in the store by definition - matching happens
    // there, and the archive's hits are written on the way through. `remote`
    // would put a MAM fetch in front of every step, and in a busy room each
    // keypress cancels the last one's before it lands.
    function jumpTo(ts, matches) {
        if (!ts)
            return
        chatModel.highlightMatches(ts, matches)
        chatModel.gotoTimestamp(ts, "local")
    }

    // Finding a message in this conversation. The hits are walked one at a time
    // in the feed itself rather than listed elsewhere, so the conversation
    // around a hit stays on screen.
    property bool searchMode: false
    // Which hit the feed is showing, -1 before the first result lands.
    property int hitIndex: -1
    // Set while a step is waiting on the page that will contain its target.
    property bool hitPending: false
    // Whether this chat's searches also ask its archive. Off by default: it is
    // a round trip per search, and the store already holds every OMEMO message,
    // which reaches the server encrypted and can be matched nowhere else.
    property bool searchServer: false

    readonly property bool hasOlderHit: hitIndex + 1 < chatSearch.count
                                        || !chatSearch.complete
    readonly property bool hasNewerHit: hitIndex > 0

    SearchModel {
        id: chatSearch
        backend: App.backend
        account: page.account
        // Named only while the bar is up: naming a chat asks its archive
        // whether it can search, which is not a question a chat that is merely
        // open needs answered.
        chat: page.searchMode ? page.chatJid : ""
        query: searchInput.text
    }

    // Long enough that a burst of typing is one search, short enough that a
    // pause answers straight away.
    Timer {
        id: searchDebounce
        interval: 250
        onTriggered: page.runSearch(page.searchServer)
    }

    function openSearch() {
        if (!page.hasChat)
            return
        page.clearSelection()
        page.searchMode = true
        searchInput.forceActiveFocus()
        searchInput.selectAll()
    }

    // Answers whether it had anything to close, so the back chain knows whether
    // the press was spent here.
    function closeSearch() {
        if (!page.searchMode)
            return false
        page.searchMode = false
        page.dropHits()
        return true
    }

    function dropHits() {
        searchDebounce.stop()
        page.hitIndex = -1
        page.hitPending = false
        chatSearch.clear()
        chatModel.highlightMatches(0, [])
    }

    // The model drops includeServer where the archive advertises no full-text
    // field, so this is what was asked for rather than what will happen.
    function runSearch(includeServer) {
        searchDebounce.stop()
        chatSearch.alsoRemote = includeServer
        page.hitIndex = -1
        // The first result to land is the one to show, and that is the same
        // step every later one takes.
        page.hitPending = true
        chatSearch.search()
    }

    function showHit(i) {
        page.hitIndex = i
        page.jumpTo(chatSearch.timestampAt(i), chatSearch.matchesAt(i))
    }

    // Older is further from the tail, which is further down the result list -
    // the store returns them newest first. Stepping past the last loaded hit
    // pages for more rather than stopping at the page boundary.
    function olderHit() {
        if (page.hitIndex + 1 < chatSearch.count) {
            page.showHit(page.hitIndex + 1)
            return
        }
        if (!chatSearch.complete) {
            page.hitPending = true
            chatSearch.loadMore()
        }
    }
    function newerHit() {
        if (page.hitIndex > 0)
            page.showHit(page.hitIndex - 1)
    }

    // Return steps through the hits, shifted the other way. Pressed before the
    // pause has elapsed it searches instead, so the first press after typing is
    // never swallowed.
    function stepFromKey(event) {
        event.accepted = true
        if (searchDebounce.running) {
            page.runSearch(page.searchServer)
        } else if (event.modifiers & Qt.ShiftModifier) {
            page.newerHit()
        } else {
            page.olderHit()
        }
    }

    Connections {
        target: chatSearch
        // Both the first page and every page after it land here: whatever was
        // waiting for a hit to exist takes the next one along.
        function onCountChanged() {
            if (!page.hitPending || chatSearch.count <= page.hitIndex + 1)
                return
            page.hitPending = false
            page.showHit(page.hitIndex + 1)
        }
    }

    function scrollToHighlight() {
        const row = chatModel.rowOfTimestamp(page.highlightTs)
        if (row >= 0)
            feed.positionViewAtIndex(row, ListView.Center)
    }

    AuthorNames {
        id: authors
        backend: App.backend
        account: page.account
        chat: page.chatJid
    }

    // Rows carry a from_jid; tacky resolves the name behind it. Until the map
    // lands the JID stands in for it, which is what tacky falls back to anyway.
    function authorName(jid) {
        if (jid === "")
            return ""
        const known = authors.names[jid]
        return known !== undefined && known !== "" ? known : jid
    }
    function selfOrAuthorName(jid) {
        return jid === "" || jid === page.account ? "You" : authorName(jid)
    }

    function fmtTime(ts) {
        if (!ts) return ""
        const d = new Date(ts / 1000) // tacky timestamps are microseconds
        return ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
    }
    // Two independent hops: server_status covers the one to our own server,
    // remote_status what the far end then did. A message still on its way to
    // the server has nothing to say about the peer, so that comes first.
    function fmtStatus(server, remote) {
        if (server === "failed")
            return "failed"
        if (server !== "")
            return "pending" // pending or uploading
        if (remote === "read")
            return "read"
        return remote === "delivered" ? "delivered" : "sent"
    }

    // Only our own messages can be sent again, and only the ones that did not
    // get out.
    function canRetry(outgoing, status) {
        return outgoing && status === "failed"
    }

    // The escape hatch for a message the encryption itself refused: the keys
    // were missing or unusable and no amount of waiting will change it, so the
    // choice is to send this one in the clear or not at all. A delivery failure
    // has nothing to do with encryption, and a message still on its way is not
    // stuck - both take the plain retry instead. fail_reason is read alongside
    // the status because it outlives the failure that set it.
    function canResendPlain(outgoing, status, encryption, failReason) {
        return outgoing && status === "failed"
            && encryption === "omemo" && failReason === "encrypt"
    }
    function sendCurrent() {
        const t = input.text.trim()
        if (t.length === 0) return
        chatModel.send(t, page.replyTo)
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
            visible: !page.selectionMode && !page.searchMode
            IconButton {
                iconPath: Icons.chevronLeft
                iconSize: 26
                Accessible.name: qsTr("Back")
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
            // 1:1 only - tacky rings a bare JID over Jingle Message Initiation,
            // which has no meaning for a room.
            IconButton {
                visible: page.hasChat && !page.chatGroupchat
                Accessible.name: qsTr("Call")
                iconPath: Icons.call
                iconSize: 20
                glyphColor: Theme.positive
                onClicked: App.calls.start(page.account, page.chatJid)
            }
            IconButton {
                objectName: "chatSearchButton"
                text: "🔍"
                font.pixelSize: 16
                glyphColor: Theme.textDim
                visible: page.hasChat
                onClicked: page.openSearch()
            }
            IconButton {
                iconPath: Icons.openInNew
                Accessible.name: qsTr("Pop out")
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
                iconPath: Icons.close
                Accessible.name: qsTr("Clear selection")
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
                iconPath: Icons.contentCopy
                iconSize: 20
                Accessible.name: qsTr("Copy")
                glyphColor: Theme.accentDeep
                onClicked: page.copySelected()
            }
        }

        // The search bar takes the header rather than opening a pane, so the
        // conversation the hits sit in stays where it was.
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 8
            spacing: 4
            visible: page.searchMode
            IconButton {
                iconPath: Icons.close
                Accessible.name: qsTr("Close search")
                onClicked: page.closeSearch()
            }
            TextField {
                id: searchInput
                objectName: "chatSearchField"
                Layout.fillWidth: true
                placeholderText: "Search this chat"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextChanged: {
                    if (searchInput.text === "")
                        page.dropHits()
                    else
                        searchDebounce.restart()
                }
                // Ahead of the field's own handling, so Return steps rather
                // than accepting.
                Keys.onReturnPressed: (event) => page.stepFromKey(event)
                Keys.onEnterPressed: (event) => page.stepFromKey(event)
                Keys.onEscapePressed: page.closeSearch()
            }
            // Named rather than drawn: no glyph says "search the server's copy
            // too" without a legend, and this is a switch a user meets once.
            // Only where the archive advertises a full-text field; anywhere
            // else the ask would come back unsupported.
            Button {
                objectName: "serverLeg"
                visible: chatSearch.remoteAvailable
                implicitHeight: 28
                leftPadding: 10
                rightPadding: 10
                // The page holds the state, so this is not `checkable` - a
                // button with its own checked state as well would leave two
                // answers to the same question.
                onClicked: {
                    page.searchServer = !page.searchServer
                    if (searchInput.text !== "")
                        page.runSearch(page.searchServer)
                }
                contentItem: Text {
                    text: "Server"
                    color: page.searchServer ? Theme.textOnAccent : Theme.textDim
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: height / 2
                    color: page.searchServer ? Theme.accentDeep : "transparent"
                    border.width: 1
                    border.color: page.searchServer ? Theme.accentDeep : Theme.hairline
                }
            }
            Text {
                objectName: "hitCounter"
                Layout.minimumWidth: 44
                horizontalAlignment: Text.AlignHCenter
                color: Theme.textDim
                font.pixelSize: 12
                text: {
                    if (chatSearch.searching && chatSearch.count === 0)
                        return "…"
                    if (chatSearch.failed)
                        return "!"
                    if (!chatSearch.searched)
                        return ""
                    if (chatSearch.count === 0)
                        return "none"
                    // A trailing + where there are pages left: the total is only
                    // what has been fetched so far, not what the archive holds.
                    return (page.hitIndex + 1) + "/" + chatSearch.count
                           + (chatSearch.complete ? "" : "+")
                }
            }
            // Chevrons now that these are drawn: as text they had to be
            // triangles, since ⌃ and ⌄ sit at opposite ends of their own em
            // box and so never lined up as a pair.
            IconButton {
                objectName: "olderHit"
                iconPath: Icons.keyboardArrowUp
                iconSize: 16
                Accessible.name: qsTr("Previous match")
                enabled: chatSearch.searched && page.hasOlderHit
                opacity: enabled ? 1 : 0.35
                onClicked: page.olderHit()
            }
            IconButton {
                objectName: "newerHit"
                iconPath: Icons.keyboardArrowDown
                iconSize: 16
                Accessible.name: qsTr("Next match")
                enabled: page.hasNewerHit
                opacity: enabled ? 1 : 0.35
                onClicked: page.newerHit()
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

    // A call that never got off the ground has no sid and so no call window to
    // report from. Only the page showing that very chat says so, which keeps a
    // pop-out and its shell from both piping up.
    property string callNotice: ""
    onChatJidChanged: callNotice = ""

    Connections {
        target: App.calls
        function onStartFailed(account, peer, message) {
            if (account === page.account && peer === page.chatJid)
                page.callNotice = message
        }
        function onMicrophoneDenied(account, peer) {
            if (account === page.account && peer === page.chatJid)
                page.callNotice = qsTr("Microphone access is off for Quack.")
        }
    }

    Rectangle {
        id: notice
        z: 5
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 10
        width: Math.min(parent.width - 24, 420)
        height: noticeText.implicitHeight + 20
        radius: 8
        color: Theme.surface
        border.width: 1
        border.color: Theme.negative
        visible: page.callNotice !== ""

        Text {
            id: noticeText
            anchors.centerIn: parent
            width: parent.width - 24
            text: page.callNotice
            color: Theme.textPrimary
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
        TapHandler { onTapped: page.callNotice = "" }
        Timer {
            running: notice.visible
            interval: 6000
            onTriggered: page.callNotice = ""
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
            objectName: "chatFeed"
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

            // BottomToTop lays row 0 (the newest) along the content's bottom, so
            // the oldest row is at the content top: the oldest edge is
            // atYBeginning, the newest atYEnd. These measure the unseen content
            // past each edge and hit zero exactly on it; under-tall content pins
            // contentY and drives olderBuffer negative.
            //
            // Functions, not bound properties: a binding refreshes off the same
            // contentYChanged the handler below runs on, and the handler wins
            // that race often enough to read the pre-scroll value.
            function olderBuffer() { return contentY - originY + topMargin }
            function newerBuffer() {
                return originY + contentHeight + bottomMargin - height - contentY
            }
            readonly property real fillThreshold: Math.max(400, height)
            property bool olderExhausted: false

            // A cursorless initial load returns only the contiguous local tail,
            // which need not even fill the viewport, so pull older pages until
            // there is a screenful of slack above or the archive runs dry.
            // ChatModel refuses a second `old` request while one is out, so the
            // view needs no in-flight latch of its own - and must not keep one,
            // since a request can stay out forever (see loadingOlder).
            function topUp() {
                if (!page.hasChat || olderExhausted || count === 0)
                    return
                if (olderBuffer() >= fillThreshold)
                    return
                chatModel.loadOlder()
            }

            Connections {
                target: chatModel
                // A reload (chat/account switch) empties the window; a fresh chat
                // may again be under-tall, so clear the exhausted latch and refill.
                function onChatChanged() { feed.olderExhausted = false }
                function onAccountChanged() { feed.olderExhausted = false }
                function onLoaded(dir, added) {
                    // A goto replaces the window, so what we knew about its old
                    // end is gone; the new slice will trigger its own fill.
                    if (dir === "goto") {
                        feed.olderExhausted = false
                        return
                    }
                    if (dir !== "init" && dir !== "old")
                        return
                    // A fresh newest page is a brand-new window: whatever we knew
                    // about the old end no longer holds, so clear the latch.
                    if (dir === "init")
                        feed.olderExhausted = false
                    // An older pull that added nothing means the archive is dry;
                    // stop, so we don't spin re-requesting an empty page.
                    else if (added === 0)
                        feed.olderExhausted = true
                }
            }
            // contentHeight, not count: rows land before the view lays them
            // out, and topUp reading the geometry from before the page would
            // see no slack above and pull another one it does not need.
            onContentHeightChanged: Qt.callLater(topUp)
            onHeightChanged: Qt.callLater(topUp)

            delegate: Item {
                id: wrap
                required property string body
                required property string markup
                required property string from
                required property string replyBody
                required property string replyAuthor
                required property bool outgoing
                required property string serverStatus
                required property string remoteStatus
                required property string encryption
                required property string failReason
                required property var timestamp
                required property var reactions
                required property var attachments
                // A share with no caption still has to copy and select as
                // something; its filename is what the user sees.
                readonly property string label: wrap.body === "" && wrap.attachments.length > 0
                    ? wrap.attachments[0].name : wrap.body
                width: feed.width
                height: bubble.height
                ChatBubble {
                    id: bubble
                    width: feed.width
                    text: wrap.body
                    markup: wrap.markup
                    author: page.authorName(wrap.from)
                    // Rooms have many voices; a 1:1 has only the two, already
                    // named by the header and the bubble side.
                    showAuthor: page.chatGroupchat && !wrap.outgoing
                    attachments: wrap.attachments
                    onAttachmentOpenRequested: (idx) => chatModel.openAttachment(wrap.timestamp, idx)
                    onAttachmentLoadRequested: (idx) => chatModel.loadAttachment(wrap.timestamp, idx)
                    replyBody: wrap.replyBody
                    replyAuthor: page.selfOrAuthorName(wrap.replyAuthor)
                    highlighted: page.highlightTs === wrap.timestamp
                    onQuoteTapped: chatModel.gotoReplyTarget(wrap.timestamp)
                    outgoing: wrap.outgoing
                    time: page.fmtTime(wrap.timestamp)
                    status: page.fmtStatus(wrap.serverStatus, wrap.remoteStatus)
                    encrypted: wrap.encryption === "omemo"
                    // A room never encrypts, so nothing in one is remarkable.
                    chatEncrypting: omemo.available && omemo.enabled
                    canRetry: page.canRetry(wrap.outgoing, status)
                    canResendPlain: page.canResendPlain(wrap.outgoing, status,
                                                        wrap.encryption, wrap.failReason)
                    onRetryRequested: chatModel.resend(wrap.timestamp, false)
                    onResendPlainRequested: chatModel.resend(wrap.timestamp, true)
                    selectionMode: page.selectionMode
                    selected: page.isSelected(wrap.timestamp)
                    reactions: wrap.reactions
                    onToggleRequested: page.toggle(wrap.timestamp, wrap.label)
                    onCopyRequested: page.copyText(wrap.label)
                    onReplyRequested: page.startReply(wrap.timestamp, wrap.label, wrap.outgoing)
                    onReactRequested: (emoji) => chatModel.react(wrap.timestamp, emoji)
                    onViewXmlRequested: page.viewXml(wrap.timestamp)
                }
            }

            // The model's load* calls are idempotent (guarded by in-flight tags).
            onContentYChanged: {
                // An empty page only proved the archive was dry at that moment,
                // and a `before` cursor is what reaches past the local rows into
                // MAM, so reaching the oldest edge is worth one more try. Only
                // on the edge, so a dry archive costs a request per arrival
                // there rather than one per pixel.
                if (olderBuffer() <= 0)
                    olderExhausted = false
                topUp()
                // Nothing newer to page for while the window holds the tail:
                // live <New> events land there on their own.
                if (!chatModel.atTail && newerBuffer() < fillThreshold)
                    chatModel.loadNewer()
            }

            // Jumping to a reply's target leaves the tail, and paging back is a
            // long way, so offer the one-tap route the backend already has.
            Rectangle {
                objectName: "jumpToLatest"
                parent: feed
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 14
                anchors.bottomMargin: 12
                width: 38
                height: 38
                radius: 19
                color: Theme.surface
                border.color: Theme.hairline
                opacity: chatModel.atTail ? 0 : 1
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 150 } }

                Glyph {
                    anchors.centerIn: parent
                    path: Icons.keyboardArrowDown
                    color: Theme.textDim
                    size: 22
                }
                TapHandler { onTapped: chatModel.resetToBottom() }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }

            // Floats over the oldest edge instead of riding along as a footer:
            // content that came and went with the request would move the very
            // edge the paging measures against. A `before` page can stay out for
            // a long time - offline it never answers - so say so rather than
            // look like the history simply ended.
            Rectangle {
                id: olderPill
                objectName: "olderPill"
                // A ListView's declared children land in its scrolling
                // contentItem; this one belongs to the viewport.
                parent: feed
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 8
                width: label.width + 32
                height: 26
                radius: 13
                color: Theme.surface
                opacity: chatModel.loadingOlder && !feed.olderExhausted ? 0.95 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 150 } }

                // Not a BusyIndicator: the Basic style paints that from its own
                // palette, which ignores Theme. Animator, so a stalled fetch
                // spins on the render thread and costs the GUI one nothing.
                Item {
                    id: spinner
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    width: 12
                    height: 12
                    RotationAnimator on rotation {
                        running: olderPill.visible
                        loops: Animation.Infinite
                        from: 0; to: 360; duration: 900
                    }
                    Repeater {
                        model: 8
                        Rectangle {
                            required property int index
                            readonly property real angle: index * Math.PI / 4
                            width: 3; height: 3; radius: 1.5
                            color: Theme.accent
                            // Fading around the ring gives the spin a direction.
                            opacity: 0.15 + 0.85 * index / 7
                            x: (spinner.width - width) / 2 * (1 + Math.cos(angle))
                            y: (spinner.height - height) / 2 * (1 + Math.sin(angle))
                        }
                    }
                }
                Text {
                    id: label
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: spinner.right
                    anchors.leftMargin: 6
                    text: "Loading"
                    color: Theme.textDim
                    font.pixelSize: 11
                }
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
                    iconPath: Icons.close
                    iconSize: 16
                    Accessible.name: qsTr("Cancel reply")
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
                // Beside the box you type in, because it says how what you are
                // typing will go out. Not an IconButton: that is a ToolButton,
                // and its own press handling would swallow the long press.
                Item {
                    objectName: "omemoToggle"
                    visible: omemo.available && page.hasChat
                    Layout.preferredWidth: visible ? 32 : 0
                    Layout.fillHeight: true
                    Text {
                        anchors.centerIn: parent
                        // As on the bubbles: the colour form of the glyph, so
                        // the shape carries the state rather than a tint.
                        text: omemo.enabled ? "🔒" : "🔓"
                        opacity: omemo.enabled ? 1 : 0.55
                        font.pixelSize: 20
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: omemo.enabled = !omemo.enabled
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: lockMenu.popup()
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onLongPressed: lockMenu.popup()
                    }

                    // The keys live behind the control that says whether they
                    // are being used - the same pairing the chat menu has.
                    Menu {
                        id: lockMenu
                        objectName: "lockMenu"
                        width: 170
                        background: Rectangle {
                            color: Theme.surface
                            radius: 10
                            border.color: Theme.hairline
                        }
                        MenuItem {
                            id: keysEntry
                            objectName: "keysEntry"
                            height: 40
                            text: "OMEMO keys…"
                            contentItem: Text {
                                text: keysEntry.text
                                color: Theme.textPrimary
                                font.pixelSize: 14
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 8
                            }
                            background: Rectangle {
                                color: keysEntry.highlighted ? Theme.menuHover : "transparent"
                                radius: 6
                            }
                            onTriggered: page.openKeys()
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 20
                    color: Theme.field
                    TextField {
                        id: input
                        objectName: "messageInput"
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
                    Glyph {
                        anchors.centerIn: parent
                        path: Icons.send
                        color: Theme.textOnAccent
                        size: 20
                    }
                    MouseArea { anchors.fill: parent; onClicked: page.sendCurrent() }
                }
            }
        }
    }
}
