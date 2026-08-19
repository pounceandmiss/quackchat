pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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

    // Who they are and what their keys are, hosted the way this app hosts every
    // other page: a window where there are windows, a full-screen sheet where
    // there are not. The page is instantiated from both the shell and a
    // pop-out, so neither of them has to know which.
    //
    // A room has no contact behind it - no roster entry, no keys of its own -
    // so it has no details to show.
    function openContact() {
        if (!page.hasChat || page.chatGroupchat)
            return null
        if (Theme.mobile) {
            contactSheet.open()
            return null
        }
        return AppWindows.contactDetails(page.account, page.chatJid,
                                         page.chatName)
    }

    // Answers whether it had anything to close, so the Android back chain knows
    // whether the press was spent here.
    function closeContact() {
        if (!contactSheet.opened)
            return false
        contactSheet.close()
        return true
    }

    FullScreenSheet {
        id: contactSheet
        objectName: "contactSheet"

        ContactDetailsPage {
            anchors.fill: parent
            account: page.account
            jid: page.chatJid
            name: page.chatName
            onDone: contactSheet.close()
        }
    }

    // What the contact page above is for a conversation of two: the room this
    // chat is, and everyone in it. A chat is one or the other, so the two sit
    // behind the same corner of the header and are never both offered.
    function openDetails() {
        if (!page.hasChat || !page.chatGroupchat)
            return null
        if (Theme.mobile) {
            detailsSheet.open()
            return null
        }
        return AppWindows.mucDetails(page.account, page.chatJid, page.chatName)
    }

    // Same contract as closeContact, except that the room's own filter unwinds
    // first, so one back press does not close both it and the sheet.
    function closeDetails() {
        if (!detailsSheet.opened)
            return false
        if (detailsPage.closeFilter())
            return true
        detailsSheet.close()
        return true
    }

    FullScreenSheet {
        id: detailsSheet
        objectName: "detailsSheet"

        MucDetailsPage {
            id: detailsPage
            anchors.fill: parent
            account: page.account
            jid: page.chatJid
            name: page.chatName
            onDone: detailsSheet.close()
        }
    }

    // One message's stanza, hosted the two ways openContact hosts the contact.
    // The stanza has to be asked for, so the viewer opens on the answer rather
    // than on the tap; the token is which answer is ours, since a second window
    // on this chat listens to the same model.
    property int xmlToken: 0

    function viewXml(ts) {
        page.xmlToken = page.chatModel ? page.chatModel.requestRawXml(ts) : 0
        // Nothing was asked, and the tap still has to end in a viewer.
        if (page.xmlToken === 0)
            page.showXml("")
    }

    // The text is handed over rather than bound, so the viewer keeps showing
    // the stanza as it stood when it was asked for.
    function showXml(xml) {
        if (Theme.mobile) {
            xmlSheet.xml = xml
            xmlSheet.open()
            return
        }
        AppWindows.messageXml(xml)
    }

    // Same contract as closeContact.
    function closeXml() {
        if (!xmlSheet.opened)
            return false
        xmlSheet.close()
        return true
    }

    FullScreenSheet {
        id: xmlSheet
        objectName: "xmlSheet"

        // "" for a message that never had a stanza built.
        property alias xml: xmlPage.xml

        MessageXmlPage {
            id: xmlPage
            anchors.fill: parent
            onDone: xmlSheet.close()
        }
    }

    // The conversation itself belongs to the app, not to this view of it: the
    // shell and a pop-out window on the same chat get the same session, and so
    // read one history window and compose into one draft. Null until a chat is
    // open, which every use below has to allow for.
    readonly property ChatSession session: App.chatFor(page.account, page.chatJid,
                                                       page.chatGroupchat)
    readonly property ChatModel chatModel: page.session ? page.session.messages : null
    readonly property OmemoChat omemo: page.session ? page.session.omemo : null

    // What the chrome reads instead of going to the model directly, since there
    // is no model until a chat is open. The answers an empty pane wants: it is
    // at the tail, fetching nothing, and encrypting nothing.
    readonly property bool atTail: page.chatModel ? page.chatModel.atTail : true
    readonly property bool loadingOlder: page.chatModel ? page.chatModel.loadingOlder : false
    readonly property string loadError: page.chatModel ? page.chatModel.loadError : ""
    readonly property bool online: page.chatModel ? page.chatModel.online : false
    readonly property bool canEncrypt: page.omemo ? page.omemo.available : false
    readonly property bool encryptOn: page.omemo ? page.omemo.enabled : false
    // `encryptOn` is tacky's default until the read answers, so a padlock drawn
    // from it before then is a guess.
    readonly property bool encryptKnown: page.omemo ? page.omemo.known : false

    // Pushed onto the shared model rather than declared with it. The palette's
    // literals, since the markup wants CSS colors and the typed accessors would
    // hand over QColors.
    Binding {
        target: page.chatModel
        property: "quoteColor"
        value: Theme.p.quote
    }
    Binding {
        target: page.chatModel
        property: "matchColor"
        value: Theme.p.selection
    }
    // Scaled the way Avatar scales its sourceSize. The model is shared, so with
    // two screens of differing ratios the last page to bind wins.
    Binding {
        target: page.chatModel
        property: "thumbMax"
        value: Math.round(Theme.thumbSize * page.Screen.devicePixelRatio)
    }

    // ChatModel has no "selected" role, so selection lives here, keyed by each
    // message's timestamp id. Every change swaps in a fresh object so the `var`
    // binding re-evaluates and delegates re-read isSelected().
    // The value is the row as the delegate saw it, not just its body: the header
    // acts on a selected message long after its row has scrolled out of the list.
    property var selectedRows: ({})
    readonly property int selectedCount: Object.keys(selectedRows).length
    readonly property bool selectionMode: selectedCount > 0

    // The header's per-message actions only mean anything with one message
    // picked out - there is no answering two of them at once. Null otherwise,
    // which is what those buttons watch.
    readonly property real loneTs: selectedCount === 1
        ? Number(Object.keys(selectedRows)[0]) : 0
    readonly property var loneRow: loneTs !== 0 ? selectedRows[loneTs] : null

    // The message a long press selected on its way to offering its actions,
    // while that is still all the selection amounts to. Taking one of those
    // actions says the selection was a side effect rather than the point, so it
    // goes again - what survives is a selection built by tapping.
    property real armedTs: 0
    // The one message that has been handed over to text selection.
    property real textSelectTs: 0
    // The one showing a menu. Here rather than in the row because a touch on any
    // other row has to know about it: that touch closes this menu and does
    // nothing else, which is the whole of what a press outside means.
    property real menuTs: 0

    // Deferred by one turn of the loop, and that delay is the point: Qt closes
    // the menu the instant a press lands outside it, while the press is still
    // on its way to whatever it landed on, which has to see the menu was up.
    function forgetMenu(ts) {
        if (page.menuTs !== ts)
            return
        Qt.callLater(function() {
            if (page.menuTs === ts)
                page.menuTs = 0
        })
    }

    function isSelected(ts) { return selectedRows[ts] !== undefined }
    function toggle(ts, row) {
        armedTs = 0
        textSelectTs = 0
        const next = Object.assign({}, selectedRows)
        if (next[ts] !== undefined)
            delete next[ts]
        else
            next[ts] = row
        selectedRows = next
    }
    function armSelection(ts, row) {
        if (!isSelected(ts))
            toggle(ts, row)
        armedTs = ts
    }
    function dropArmedSelection() {
        if (armedTs !== 0 && selectedCount === 1 && isSelected(armedTs))
            clearSelection()
        armedTs = 0
    }
    function clearSelection() {
        armedTs = 0
        textSelectTs = 0
        selectedRows = ({})
    }
    function copySelected() {
        // Object keys iterate in ascending numeric order, so this joins the
        // chosen messages oldest-first regardless of the tap order.
        Clipboard.setText(Object.values(selectedRows).map(r => r.body).join("\n"))
        clearSelection()
    }

    // The header's actions all finish with the message they acted on, so each
    // of them ends the selection that offered it. Read out first: clearing is
    // what empties loneTs and loneRow.
    function actOnLone(fn) {
        const ts = page.loneTs
        const row = page.loneRow
        if (!row)
            return
        clearSelection()
        fn(ts, row)
    }

    // The message being answered lives on the session, so it survives the chat
    // being closed and reopened. Read back through here, which is also where the
    // no-chat-open case is handled once rather than at each use.
    readonly property bool replying: page.session ? page.session.replying : false
    readonly property string replyBody: page.session ? page.session.replyBody : ""
    readonly property bool replyOutgoing: page.session ? page.session.replyOutgoing : false

    function startReply(ts, body, outgoing) {
        if (!page.session)
            return
        page.session.replyToMessage(ts, body, outgoing)
        input.forceActiveFocus()
    }
    function cancelReply() {
        if (page.session)
            page.session.cancelReply()
    }

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
    readonly property bool reading: page.session !== null && visible
                                    && Qt.application.state === Qt.ApplicationActive // qmllint disable missing-property
                                    && page.atTail
    onReadingChanged: if (reading) page.chatModel.markRead()

    Connections {
        target: page.chatModel
        // Every new row while the chat is being read moves the watermark; tacky
        // holds its alert briefly so this lands first and nothing fires.
        function onRowsInserted() {
            if (page.reading)
                page.chatModel.markRead()
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
        function onRawXmlReady(token, xml) {
            if (token !== page.xmlToken) // another window's viewer
                return
            page.xmlToken = 0
            page.showXml(xml)
        }
        // The model resolved an attachment to a file on disk; handing it to the
        // desktop is the one part of opening it that has to happen up here.
        function onAttachmentResolved(url) {
            if (url != "")
                Qt.openUrlExternally(url)
        }
        // Qt has no "reveal this file", so the folder holding it is opened
        // instead.
        function onAttachmentFolder(url) {
            if (url != "")
                Qt.openUrlExternally(url)
        }
        // By now the dialog that asked has been gone a while, so the reason on
        // its own ("Destination file exists") would say nothing about what was
        // being attempted.
        function onAttachmentSaved(dest, error) {
            if (error === "")
                return
            saveFailed.message = "Save failed: " + error
            saveFailed.open()
        }
    }

    SheetDialog {
        id: saveFailed
        objectName: "saveFailedDialog"
        property alias message: saveFailedText.text
        standardButtons: Dialog.Ok
        Text {
            id: saveFailedText
            objectName: "saveFailedMessage"
            width: saveFailed.availableWidth
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
    }

    // No filters: tacky puts up whatever it is handed.
    FileDialog {
        id: attachDialog
        objectName: "attachDialog"
        title: "Attach a file"
        onAccepted: if (page.chatModel)
            page.chatModel.sendFile(attachDialog.selectedFile)
    }

    FileDialog {
        id: saveDialog
        objectName: "attachmentSaveDialog"
        title: "Save attachment"
        fileMode: FileDialog.SaveFile
        onAccepted: if (page.chatModel)
            page.chatModel.saveAttachment(page.savingTs, page.savingIdx,
                                          saveDialog.selectedFile)
    }

    // The whole page takes them, not just the composer: the gesture is at the
    // conversation, not at the box you type in.
    DropArea {
        id: fileDrop
        objectName: "fileDrop"
        anchors.fill: parent
        enabled: page.hasChat
        onDropped: (drop) => {
            if (!drop.hasUrls || !page.chatModel)
                return
            for (const url of drop.urls)
                page.chatModel.sendFile(url)
            drop.acceptProposedAction()
        }

        Rectangle {
            anchors.fill: parent
            visible: fileDrop.containsDrag
            color: Theme.accent
            opacity: 0.12
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
        page.chatModel.highlightMatches(ts, matches)
        page.chatModel.gotoTimestamp(ts, "local")
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
        // A query left in the bar would name results that went with it.
        searchInput.clear()
        page.dropHits()
        return true
    }

    function dropHits() {
        searchDebounce.stop()
        page.hitIndex = -1
        page.hitPending = false
        chatSearch.clear()
        // Reachable with no chat open: the field's own onTextChanged calls this
        // when it is cleared.
        if (page.chatModel)
            page.chatModel.highlightMatches(0, [])
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
    // pause has elapsed, or with a query nothing has answered, it searches
    // instead, so a press is never swallowed.
    function stepFromKey(event) {
        event.accepted = true
        if (searchDebounce.running
                || (searchInput.text !== "" && !chatSearch.searched)) {
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
        // waiting for a hit to exist takes the next one along. Off the
        // arrival, not the count: a re-search that finds as many hits as it
        // replaces never changes it.
        function onResultsArrived() {
            if (!page.hitPending || chatSearch.count <= page.hitIndex + 1)
                return
            page.hitPending = false
            page.showHit(page.hitIndex + 1)
        }
    }

    function scrollToHighlight() {
        const row = page.chatModel.rowOfTimestamp(page.highlightTs)
        if (row >= 0)
            feed.centreOnRow(row)
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

    // Which of the two retries a row wants, from the direction tacky gave the
    // transfer that failed rather than from anything read off the row itself.
    // An upload that did not land has no message for `resend` to send.
    function isFailedUpload(att) {
        return att !== undefined && att.direction === "upload"
            && att.state === "failed"
    }
    function retryMessage(ts, attachments) {
        if (!page.chatModel)
            return
        if (page.isFailedUpload(attachments[0]))
            page.chatModel.retryUpload(ts)
        else
            page.chatModel.resend(ts, false)
    }
    function retryAttachment(ts, attachments, idx) {
        if (!page.chatModel)
            return
        if (page.isFailedUpload(attachments[idx]))
            page.chatModel.retryUpload(ts)
        else
            page.chatModel.loadAttachment(ts, idx)
    }

    // Which attachment the save dialog is out for; it reports only the path.
    property real savingTs: 0
    property int savingIdx: 0
    function askWhereToSave(ts, attachments, idx) {
        page.savingTs = ts
        page.savingIdx = idx
        const folder = saveDialog.currentFolder.toString()
        saveDialog.selectedFile = (folder !== "" ? folder + "/" : "")
                                + attachments[idx].name
        saveDialog.open()
    }
    function sendCurrent() {
        if (!page.session)
            return
        // The session holds the draft and the reply it answers, so it does the
        // whole send and clears both; the field only has to catch up.
        page.session.sendDraft()
        input.clear()
    }

    // The field is seeded from the session's draft and writes back to it, rather
    // than being bound both ways: typing has to break a binding on `text`, so a
    // live one would not survive the first keystroke. Re-seeding on every change
    // of session is what carries a half-written message from the shell into a
    // pop-out - and what keeps it from following you into the next chat.
    onSessionChanged: input.text = page.session ? page.session.draft : ""

    header: PageHeader {
        // No chat, no header - the empty pane draws its own invitation.
        implicitHeight: page.hasChat ? 60 : 0
        visible: page.hasChat

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
            // The picture and the name are what the chat's own page is about, so
            // they are also what opens it - the contact behind a conversation of
            // two, and the room behind one of many.
            Item {
                id: chatIdentity
                objectName: "chatIdentity"
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Pressed feedback, short of the bar's own edges so it reads as
                // a target rather than as the header changing colour.
                Rectangle {
                    anchors.fill: parent
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6
                    radius: 8
                    color: Theme.textPrimary
                    opacity: identityTap.pressed ? 0.12 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }

                HoverHandler {
                    enabled: identityTap.enabled
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    id: identityTap
                    onTapped: page.chatGroupchat ? page.openDetails()
                                                 : page.openContact()
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 10
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
                }
            }
            // Where a 1:1 chat has a call button, a room has its people.
            IconButton {
                objectName: "roomDetailsButton"
                visible: page.hasChat && page.chatGroupchat
                Accessible.name: qsTr("Room details")
                iconPath: Icons.group
                iconSize: 20
                glyphColor: Theme.textDim
                onClicked: page.openDetails()
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
                iconPath: Icons.search
                iconSize: 20
                Accessible.name: qsTr("Search this chat")
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
            // What the bubble's menu offers, drawn along the bar the selection
            // puts up, so a long press reaches all of it without one. Each acts
            // on a single message, so each waits for exactly one to be picked.
            IconButton {
                objectName: "selectionReply"
                iconPath: Icons.reply
                iconSize: 20
                Accessible.name: qsTr("Reply")
                glyphColor: Theme.accentDeep
                visible: page.loneRow !== null
                onClicked: page.actOnLone((ts, row) =>
                    page.startReply(ts, row.body, row.outgoing))
            }
            IconButton {
                objectName: "selectionRetry"
                iconPath: Icons.refresh
                iconSize: 20
                Accessible.name: qsTr("Retry")
                glyphColor: Theme.accentDeep
                visible: page.loneRow !== null && page.loneRow.canRetry
                onClicked: page.actOnLone((ts, row) =>
                    page.retryMessage(ts, row.attachments))
            }
            IconButton {
                objectName: "selectionResendPlain"
                iconPath: Icons.lockOpen
                iconSize: 20
                Accessible.name: qsTr("Send without encryption")
                // The one that gives something up, and the only one here
                // wearing the colour that says so.
                glyphColor: Theme.warning
                visible: page.loneRow !== null && page.loneRow.canResendPlain
                onClicked: page.actOnLone((ts) => page.chatModel.resend(ts, true))
            }
            IconButton {
                objectName: "selectionViewXml"
                iconPath: Icons.code
                iconSize: 20
                Accessible.name: qsTr("View XML")
                glyphColor: Theme.accentDeep
                visible: page.loneRow !== null
                onClicked: page.actOnLone((ts) => page.viewXml(ts))
            }
            IconButton {
                objectName: "selectionCopy"
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
                    // A re-search leaves the last one's hits up, so what says
                    // there is nothing to count yet is the step, not the count.
                    if (chatSearch.searching && page.hitIndex < 0)
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
            model: page.chatModel
            clip: true
            spacing: 10
            topMargin: 14
            bottomMargin: 8
            verticalLayoutDirection: ListView.BottomToTop
            cacheBuffer: 400
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ThinScrollBar {}

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

            // Rows are variable-height and laid out bottom-to-top, so where
            // centring one would leave the view is nothing to be worked out
            // here: go there, read it back, and decide whether to make the
            // trip again slowly.
            function centreOnRow(row) {
                cancelFlick()
                const from = contentY
                positionViewAtIndex(row, ListView.Center)
                const to = contentY
                // Past a screen or so it is a change of place rather than a
                // movement, often into rows the window did not hold a moment
                // ago, and sliding through those reads as a glitch. The
                // highlight is what says where we landed.
                if (Math.abs(to - from) > height * 1.5)
                    return
                contentY = from
                hitScroll.from = from
                hitScroll.to = to
                hitScroll.restart()
            }

            NumberAnimation {
                id: hitScroll
                objectName: "hitScroll"
                target: feed
                property: "contentY"
                duration: 180
                easing.type: Easing.OutCubic
            }

            // A hand on the view outranks a slide it did not ask to follow.
            onDragStarted: hitScroll.stop()
            onFlickStarted: hitScroll.stop()

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
                page.chatModel.loadOlder()
            }

            Connections {
                target: page.chatModel
                // A reload (chat/account switch) empties the window; a fresh chat
                // may again be under-tall, so clear the exhausted latch and refill.
                function onChatChanged() { feed.olderExhausted = false }
                function onAccountChanged() { feed.olderExhausted = false }
                function onLoaded(dir, added) {
                    // Rows landing above move contentY under a slide already
                    // in flight, whose remaining frames would then aim at a
                    // place that has shifted.
                    if (added !== 0)
                        hitScroll.stop()
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
                // What a selected message hands the header: everything its
                // buttons need to act without coming back to this delegate.
                readonly property var row: ({
                    body: wrap.label,
                    outgoing: wrap.outgoing,
                    attachments: wrap.attachments,
                    canRetry: bubble.canRetry,
                    canResendPlain: bubble.canResendPlain
                })
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
                    onAttachmentOpenRequested: (idx) => page.chatModel.openAttachment(wrap.timestamp, idx)
                    onAttachmentLoadRequested: (idx) => page.retryAttachment(wrap.timestamp,
                                                                             wrap.attachments, idx)
                    onAttachmentSaveRequested: (idx) => page.askWhereToSave(wrap.timestamp,
                                                                            wrap.attachments, idx)
                    onAttachmentFolderRequested: (idx) => page.chatModel.revealAttachment(wrap.timestamp, idx)
                    onAttachmentUncacheRequested: (idx) => page.chatModel.uncacheAttachment(wrap.timestamp, idx)
                    onAttachmentCancelRequested: (idx) => page.chatModel.cancelAttachment(wrap.timestamp, idx)
                    replyBody: wrap.replyBody
                    replyAuthor: page.selfOrAuthorName(wrap.replyAuthor)
                    highlighted: page.highlightTs === wrap.timestamp
                    onQuoteTapped: page.chatModel.gotoReplyTarget(wrap.timestamp)
                    outgoing: wrap.outgoing
                    time: page.fmtTime(wrap.timestamp)
                    status: page.fmtStatus(wrap.serverStatus, wrap.remoteStatus)
                    encrypted: wrap.encryption === "omemo"
                    // A room never encrypts, so nothing in one is remarkable.
                    chatEncrypting: page.canEncrypt && page.encryptOn
                    canRetry: page.canRetry(wrap.outgoing, status)
                    canResendPlain: page.canResendPlain(wrap.outgoing, status,
                                                        wrap.encryption, wrap.failReason)
                    onRetryRequested: page.retryMessage(wrap.timestamp, wrap.attachments)
                    onResendPlainRequested: page.chatModel.resend(wrap.timestamp, true)
                    selectionMode: page.selectionMode
                    selected: page.isSelected(wrap.timestamp)
                    textSelecting: page.textSelectTs === wrap.timestamp
                    reactions: wrap.reactions
                    onToggleRequested: page.toggle(wrap.timestamp, wrap.row)
                    onSelectRequested: page.armSelection(wrap.timestamp, wrap.row)
                    onActionTaken: page.dropArmedSelection()
                    menuOpen: page.menuTs === wrap.timestamp
                    anyMenuOpen: page.menuTs !== 0
                    onMenuOpened: page.menuTs = wrap.timestamp
                    onMenuClosed: page.forgetMenu(wrap.timestamp)
                    onMenuDismissRequested: page.menuTs = 0
                    onTextSelectRequested: page.textSelectTs = wrap.timestamp
                    onTextSelectEnded: page.textSelectTs = 0
                    onCopyTextRequested: (picked) => {
                        Clipboard.setText(picked)
                        page.textSelectTs = 0
                    }
                    onCopyRequested: Clipboard.setText(wrap.label)
                    onReplyRequested: page.startReply(wrap.timestamp, wrap.label, wrap.outgoing)
                    onReactRequested: (emoji) => page.chatModel.react(wrap.timestamp, emoji)
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
                if (!page.atTail && newerBuffer() < fillThreshold)
                    page.chatModel.loadNewer()
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
                opacity: page.atTail ? 0 : 1
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 150 } }

                Glyph {
                    anchors.centerIn: parent
                    path: Icons.keyboardArrowDown
                    color: Theme.textDim
                    size: 22
                }
                TapHandler { onTapped: page.chatModel.resetToBottom() }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }

            // Floats over the oldest edge instead of riding along as a footer:
            // content that came and went with the request would move the very
            // edge the paging measures against. A `before` page can stay out
            // for a long time, and can fail outright, so say which rather than
            // look like the history simply ended.
            Rectangle {
                id: olderPill
                objectName: "olderPill"
                // A ListView's declared children land in its scrolling
                // contentItem; this one belongs to the viewport.
                parent: feed
                readonly property bool failed: page.loadError !== ""
                // Offline the request is buffered rather than travelling, so
                // spinning at it would claim progress it is not making.
                readonly property bool waiting: page.loadingOlder && !page.online
                readonly property bool working: page.loadingOlder && page.online
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 8
                width: content.width + 20
                height: 26
                radius: 13
                color: Theme.surface
                opacity: (page.loadingOlder || failed) && !feed.olderExhausted
                         ? 0.95 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
                TapHandler {
                    enabled: olderPill.failed
                    onTapped: page.chatModel.retry()
                }
                HoverHandler {
                    enabled: olderPill.failed
                    cursorShape: Qt.PointingHandCursor
                }

                Row {
                    id: content
                    anchors.centerIn: parent
                    spacing: 6

                    // Not a BusyIndicator: the Basic style paints that from its
                    // own palette, which ignores Theme. Animator, so a stalled
                    // fetch spins on the render thread and costs the GUI one
                    // nothing.
                    Item {
                        id: spinner
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        visible: olderPill.working
                        RotationAnimator on rotation {
                            running: spinner.visible && olderPill.visible
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
                        objectName: "olderPillLabel"
                        anchors.verticalCenter: parent.verticalCenter
                        // The backend words this, so keep it off the viewport's
                        // edges however long it runs.
                        width: Math.min(implicitWidth, feed.width - 96)
                        elide: Text.ElideRight
                        text: olderPill.failed ? page.loadError
                            : olderPill.waiting ? "Offline"
                            : "Loading"
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    // Beside the message, since eliding one would eat it.
                    Text {
                        id: retry
                        objectName: "olderPillRetry"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: olderPill.failed
                        text: "Retry"
                        color: Theme.accent
                        font.pixelSize: 11
                    }
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
                    visible: page.canEncrypt && page.hasChat && page.encryptKnown
                    Layout.preferredWidth: visible ? 32 : 0
                    Layout.fillHeight: true
                    Text {
                        anchors.centerIn: parent
                        // As on the bubbles: the colour form of the glyph, so
                        // the shape carries the state rather than a tint.
                        text: page.encryptOn ? "🔒" : "🔓"
                        opacity: page.encryptOn ? 1 : 0.55
                        font.pixelSize: 20
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: page.omemo.enabled = !page.omemo.enabled
                    }
                    // A touch point carries no button for acceptedButtons to
                    // filter; touch has the long press below.
                    TapHandler {
                        acceptedDevices: PointerDevice.Mouse
                        acceptedButtons: Qt.RightButton
                        onTapped: lockMenu.popup()
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onLongPressed: lockMenu.popup()
                    }

                    // The keys live behind the control that says whether they
                    // are being used - the same pairing the chat menu has. They
                    // are a card on the contact's page, so this and the header
                    // lead to the same place.
                    AppMenu {
                        id: lockMenu
                        objectName: "lockMenu"
                        width: 170
                        MenuEntry {
                            objectName: "keysEntry"
                            text: "OMEMO keys…"
                            onTriggered: page.openContact()
                        }
                    }
                }
                IconButton {
                    objectName: "attachButton"
                    iconPath: Icons.attachFile
                    iconSize: 20
                    glyphColor: Theme.textDim
                    Accessible.name: qsTr("Attach a file")
                    visible: page.hasChat
                    Layout.preferredWidth: visible ? 36 : 0
                    onClicked: attachDialog.open()
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
                        // The pill is drawn by the Rectangle, so the inset is
                        // the field's own padding. A margin on top of it would
                        // stack with whatever the style pads by - 16 under
                        // Material, and the text starts a third of an inch in.
                        leftPadding: 16
                        rightPadding: 16
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        background: Item {}
                        // Label the virtual keyboard's enter key "Send" (its
                        // press still lands here as accepted).
                        EnterKey.type: Qt.EnterKeySend
                        onAccepted: page.sendCurrent()
                        onTextChanged: if (page.session) page.session.draft = text
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
