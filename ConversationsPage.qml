pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The conversations list for one account. `account` selects which per-account
// ChatListModel to bind (App.chatListFor); an empty account yields an empty list.
//
// One box asks both halves of what a typed word can mean: the chats it names,
// and the messages it matches inside them (MessageHits, under the list).
Page {
    id: page
    objectName: "conversationsPane"
    property string account: ""
    // The chat the window is showing, so its row can say so. The shell's state
    // rather than the list's: a pop-out leaves nothing selected here.
    property string currentJid: ""
    signal openChat(string jid, string name, bool groupchat)
    signal popOutChat(string jid, string name, bool groupchat)
    // matches travels with the hit so the chat can mark the run the row marked.
    signal openHit(string chatJid, real ts, var matches)
    signal openAccounts()
    background: Rectangle { color: Theme.surface }

    // The account's roster + bookmarks + history, and what every edit below goes
    // through. Null with no account, and again once one is removed - which is
    // why the handlers check it rather than assume the row they came from.
    readonly property ChatListModel chatList:
        account !== "" ? App.chatListFor(account) : null

    // connRev is read purely to give these a dependency: both lookups are calls,
    // so nothing would re-run them otherwise.
    readonly property string connState: {
        App.accounts.connRev
        return App.accounts.connStateFor(page.account)
    }
    readonly property bool accountEnabled: {
        App.accounts.connRev
        return App.accounts.isEnabled(page.account)
    }
    readonly property bool accountStatusKnown: {
        App.accounts.connRev
        return App.accounts.statusKnownFor(page.account)
    }

    // What the list actually shows: the account's chats, narrowed by the search
    // box and in the order this window was asked for. Per view, so typing here
    // leaves the same account's other windows alone.
    ChatListFilter {
        id: visibleChats
        objectName: "chatListFilter"
        source: page.chatList
        query: searchField.text
    }

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    // Ctrl+Tab's step. Asked of the filter, not the shared model: the order and
    // the surviving rows are this window's. Scrolled to, or the mark it opens
    // lands off screen.
    function cycleChat(delta) {
        const row = visibleChats.stepRow(page.currentJid, delta)
        if (row < 0)
            return
        const entry = visibleChats.entryAt(row)
        listView.positionViewAtIndex(row, ListView.Contain)
        page.openChat(entry.jid, entry.name ?? "", entry.groupchat === true)
    }

    header: PageHeader {
        // Sized by what it holds - the title row and the filter under it - so
        // neither can change height without the header following.
        implicitHeight: headerRows.implicitHeight

        ColumnLayout {
            id: headerRows
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                // Lines the badge up with the avatars in the rows below it.
                Layout.leftMargin: 16
                Layout.rightMargin: 8
                spacing: 8
                // The way to the rail, which is a drawer. The account's own face
                // rather than a hamburger: with the rail shut, this dot is the
                // only standing sign of a dropped connection.
                ToolButton {
                    id: accountsBtn
                    objectName: "accountsButton"
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    padding: 5
                    Accessible.name: qsTr("Accounts")
                    onClicked: page.openAccounts()
                    contentItem: AccountBadge {
                        objectName: "accountStatusBadge"
                        jid: page.account
                        connState: page.connState
                        acctEnabled: page.accountEnabled
                        statusKnown: page.accountStatusKnown
                        dotSize: 11
                        ringColor: Theme.surface
                    }
                    background: Rectangle {
                        color: accountsBtn.hovered ? Theme.menuHover : "transparent"
                        radius: 6
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        text: qsTr("Chats")
                        color: Theme.textPrimary
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: page.account
                        visible: page.account !== ""
                        color: Theme.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    objectName: "newChatButton"
                    iconPath: Icons.add
                    Accessible.name: qsTr("New chat")
                    glyphColor: Theme.textDim
                    enabled: page.account !== ""
                    opacity: enabled ? 1 : 0.4
                    onClicked: newChatSheet.open()
                }
                IconButton {
                    id: overflowBtn
                    iconPath: Icons.moreHoriz
                    Accessible.name: qsTr("More")
                    glyphColor: Theme.textDim
                    onClicked: overflow.popup(overflowBtn,
                                              overflowBtn.width - overflow.width,
                                              overflowBtn.height + 2)
                }
            }

            // The Tk list's search entry. Permanent rather than revealed, as
            // it is there.
            TextField {
                id: searchField
                objectName: "searchField"
                Layout.fillWidth: true
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                Layout.bottomMargin: 8
                placeholderText: qsTr("Search chats and messages")
                enabled: page.account !== ""
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                // Room for the glyph overlaid on the left and the button
                // overlaid on the right; with no button there, the far side
                // matches the near one.
                leftPadding: searchGlyph.x + searchGlyph.width + 6
                rightPadding: clearSearch.visible ? clearSearch.width + 6
                                                  : leftPadding
                // Says what the field is for, now that its frame is a quiet
                // one; it also balances the clear button across from it.
                Glyph {
                    id: searchGlyph
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    path: Icons.search
                    color: Theme.textDim
                    size: 16
                }
                IconButton {
                    id: clearSearch
                    objectName: "clearSearchButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    iconPath: Icons.close
                    Accessible.name: qsTr("Clear search")
                    glyphColor: Theme.textDim
                    visible: searchField.text !== ""
                    onClicked: searchField.clear()
                }
                Keys.onEscapePressed: searchField.clear()
            }
        }
    }

    // Android and iOS are single-window, so there the preferences get a sheet
    // over the shell rather than a window, as AccountRail does for the account.
    function openPreferences() {
        if (Theme.mobile)
            prefsSheet.open()
        else
            AppWindows.preferences()
    }

    // Window-level actions, previously a global toolbar. Ctrl+N lives on the
    // window; this menu is its mouse path. The theme is not here: it belongs
    // with the rest of Appearance, a page down in Preferences.
    AppMenu {
        id: overflow
        objectName: "overflowMenu"
        width: 190
        MenuEntry {
            objectName: "newChatEntry"
            text: qsTr("New chat…")
            enabled: page.account !== ""
            onTriggered: newChatSheet.open()
        }
        MenuEntry {
            objectName: "joinRoomEntry"
            text: qsTr("Join room…")
            enabled: page.account !== ""
            onTriggered: joinRoomSheet.open()
        }
        // The Tk list's "Sort by" submenu, flattened: two entries with a tick
        // are the whole of it. Per window and not remembered, as it is there.
        MenuEntry {
            objectName: "sortRecentEntry"
            text: qsTr("Sort by activity")
            trailing: visibleChats.sortMode === ChatListFilter.Recent ? "✓" : ""
            onTriggered: visibleChats.sortMode = ChatListFilter.Recent
        }
        MenuEntry {
            objectName: "sortNameEntry"
            text: qsTr("Sort by name")
            trailing: visibleChats.sortMode === ChatListFilter.Name ? "✓" : ""
            onTriggered: visibleChats.sortMode = ChatListFilter.Name
        }
        // The Tk account window's View menu, which is a page of its own here:
        // flattened into this one it buried the actions it sat among.
        MenuEntry {
            objectName: "preferencesEntry"
            text: qsTr("Preferences…")
            onTriggered: page.openPreferences()
        }
        MenuEntry {
            text: qsTr("New window")
            trailing: "Ctrl+N"
            offered: !Theme.mobile // single-window platforms
            onTriggered: AppWindows.newShell(page.account)
        }
        // The Tk list's Refresh: not a repaint but a re-ask, for when the
        // server and what we hold have drifted apart.
        MenuEntry {
            objectName: "refreshEntry"
            text: qsTr("Refresh")
            enabled: page.account !== ""
            onTriggered: if (page.chatList) page.chatList.reload()
        }
    }

    ListView {
        id: listView
        objectName: "chatList"
        anchors.fill: parent
        model: visibleChats
        clip: true

        ScrollBar.vertical: ThinScrollBar {}

        // A heading over the only thing here says nothing; it earns its place
        // once the hits are underneath it.
        header: SectionLabel {
            width: listView.width
            text: qsTr("Chats")
            shown: searchField.text !== "" && visibleChats.count > 0
        }

        delegate: ItemDelegate {
            id: row
            required property string jid
            required property string name
            required property bool groupchat
            required property int unread
            required property int unread_mentions
            required property string room_state
            // The whole entry, for the row menu: it wants the bookmark fields
            // too, and naming each one here would be a second copy of the
            // entry's shape.
            required property var raw
            width: ListView.view.width
            height: 64
            onClicked: page.openChat(jid, name, groupchat)

            readonly property bool current: row.jid === page.currentJid

            // Right-click used to pop the chat out; that is one entry in this
            // menu now, where the rest of the row's verbs are. A touch point
            // carries no button for acceptedButtons to filter; touch has the
            // long press below.
            TapHandler {
                acceptedDevices: PointerDevice.Mouse
                acceptedButtons: Qt.RightButton
                onTapped: rowMenu.openFor(row.raw)
            }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onLongPressed: rowMenu.openFor(row.raw)
            }

            readonly property string title: name !== "" ? name : jid

            // The Tk list's room-state row styling, in this palette's terms: a
            // room we are not in is dimmed, one on its way in is dimmed and
            // italic, a member room we have been dropped from is warned about,
            // and a join that failed is an error. Only rooms have a state.
            readonly property color titleColor: {
                if (!row.groupchat)
                    return Theme.textPrimary
                switch (row.room_state) {
                case "error":        return Theme.negative
                case "disconnected": return Theme.warning
                case "joining":
                case "idle":         return Theme.textDim
                default:             return Theme.textPrimary
                }
            }

            background: Rectangle {
                color: row.hovered ? Theme.menuHover : "transparent"

                // The open chat, tinted the way a selected message is.
                Rectangle {
                    objectName: "currentChatTint"
                    anchors.fill: parent
                    color: Theme.selection
                    opacity: row.current ? 0.45 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }
                // And the rail's current-account tab: the tint alone is easy to
                // lose beside a hovered row.
                Rectangle {
                    objectName: "currentChatTab"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: row.current ? 44 : 0
                    radius: 2
                    color: Theme.accentDeep
                    visible: height > 0
                    Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
            }

            contentItem: RowLayout {
                spacing: 12

                Avatar {
                    Layout.leftMargin: 14
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    account: page.account
                    jid: row.jid
                    label: row.title
                    initialsPixelSize: 16
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        objectName: "chatRowTitle"
                        Layout.fillWidth: true
                        text: row.title
                        color: row.titleColor
                        font.pixelSize: 16
                        font.bold: true
                        // A room mid-join, so the row reads as transient rather
                        // than as one more dimmed idle room.
                        font.italic: row.groupchat && row.room_state === "joining"
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.jid
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Glyph {
                    Layout.rightMargin: 14
                    visible: row.groupchat
                    path: Icons.group
                    color: Theme.textDim
                    size: 16
                }

                // Someone named you in there. The Tk list says this by bolding
                // the row, which is no signal here - the name is already bold -
                // so it gets a mark of its own beside the count.
                Text {
                    objectName: "mentionMark"
                    Layout.rightMargin: 14
                    visible: row.unread_mentions > 0
                    text: "@"
                    color: Theme.accent
                    font.pixelSize: 16
                    font.bold: true
                }

                // The Tk list's "Name (3)" suffix, as the badge an avatar row
                // has room for. Hidden at zero, so the layout skips it.
                Rectangle {
                    objectName: "unreadBadge"
                    Layout.rightMargin: 14
                    visible: row.unread > 0
                    implicitHeight: 20
                    implicitWidth: Math.max(height, unreadText.implicitWidth + 12)
                    radius: height / 2
                    color: Theme.accent

                    Text {
                        id: unreadText
                        objectName: "unreadCount"
                        anchors.centerIn: parent
                        // tacky counts the true total; three digits of it would
                        // eat the name.
                        //: Unread badge when the true count is over 99
                        text: row.unread > 99 ? qsTr("99+") : row.unread.toString()
                        color: Theme.textOnAccent
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }

        // In the foot of this list rather than beside it, so one flick carries
        // both halves and the chats keep the top.
        footer: MessageHits {
            width: listView.width
            account: page.account
            query: searchField.text
            chatMatches: visibleChats.count
            onOpenHit: (chatJid, ts, matches) => page.openHit(chatJid, ts, matches)
        }
    }

    // Everything a row can be asked to do. The menu holds the row it was opened
    // on, so the handlers read it from there rather than from the delegate,
    // which may well have been recycled by the time an answer comes back.
    ChatRowMenu {
        id: rowMenu
        canPopOut: !Theme.mobile
        onOpenChat: page.openChat(jid, chatTitle, groupchat)
        onPopOutChat: page.popOutChat(jid, chatTitle, groupchat)
        onStartCall: App.calls.start(page.account, jid)
        onAddContact: if (page.chatList) page.chatList.addContact(jid, "")
        onJoinRoom: if (page.chatList) page.chatList.joinRoom(jid)
        onLeaveRoom: if (page.chatList) page.chatList.leaveRoom(jid)
        onForceJoin: if (page.chatList) page.chatList.forceJoinRoom(jid)
        onRefreshAvatar: App.avatars.refresh(page.account, jid)
        onCopyJid: Clipboard.setText(jid)
        onRenameContact: {
            renamePrompt.subject = jid
            renamePrompt.prompt = qsTr("New name for %1:").arg(jid)
            renamePrompt.value = chatTitle
            renamePrompt.open()
        }
        onEditBookmark: {
            bookmarkPrompt.subject = jid
            bookmarkPrompt.prompt = qsTr("Bookmark name for %1:").arg(jid)
            bookmarkPrompt.value = chatTitle
            bookmarkPrompt.open()
        }
        onRemoveContact: {
            removeContactConfirm.subject = jid
            removeContactConfirm.message = qsTr("Remove %1 from your contacts?").arg(jid)
            removeContactConfirm.open()
        }
        onRemoveBookmark: {
            removeBookmarkConfirm.subject = jid
            removeBookmarkConfirm.message = qsTr("Remove the bookmark for %1?").arg(jid)
            removeBookmarkConfirm.open()
        }
    }

    // Full-screen rather than centred: a page of settings is a screenful.
    FullScreenSheet {
        id: prefsSheet
        objectName: "preferencesSheet"

        AppSettingsPage {
            anchors.fill: parent
            onDone: prefsSheet.close()
        }
    }

    // Adding the contact and opening the chat are separate: the chat opens
    // either way, and the row the roster write produces arrives on its own.
    NewChatSheet {
        id: newChatSheet
        onStartChat: (jid, name, addToContacts) => {
            if (addToContacts && page.chatList)
                page.chatList.addContact(jid, name)
            page.openChat(jid, name, false)
        }
    }

    // Joining is a bookmark write; the room's row arrives from the chatlist
    // event it causes, carrying the ?join suffix that opens it as a group chat.
    JoinRoomSheet {
        id: joinRoomSheet
        account: page.account
        onJoinRoom: (jid, nick, password) => {
            if (page.chatList)
                page.chatList.joinRoom(jid, nick, password)
        }
    }

    // An empty answer is the Tk dialogs' "cancelled", and a name unchanged is
    // not worth a round trip to the server.
    TextPromptDialog {
        id: renamePrompt
        objectName: "renamePrompt"
        title: qsTr("Rename contact")
        onSubmitted: (text) => {
            if (page.chatList && text !== "" && text !== value)
                page.chatList.renameContact(subject, text)
        }
    }
    TextPromptDialog {
        id: bookmarkPrompt
        objectName: "bookmarkPrompt"
        title: qsTr("Edit bookmark")
        onSubmitted: (text) => {
            if (page.chatList && text !== "" && text !== value)
                page.chatList.renameBookmark(subject, text)
        }
    }
    ConfirmDialog {
        id: removeContactConfirm
        objectName: "removeContactConfirm"
        title: qsTr("Remove contact")
        onAccepted: if (page.chatList) page.chatList.removeContact(subject)
    }
    ConfirmDialog {
        id: removeBookmarkConfirm
        objectName: "removeBookmarkConfirm"
        title: qsTr("Remove bookmark")
        onAccepted: if (page.chatList) page.chatList.removeBookmark(subject)
    }

    // Empty state, reflecting the real connection state (not just "empty").
    // What a query found is answered under the list instead.
    Text {
        objectName: "emptyHint"
        anchors.centerIn: parent
        visible: visibleChats.count === 0 && searchField.text === ""
        width: parent.width - 60
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: 14
        text: {
            if (page.account === "")
                return qsTr("No account selected.\nUse the accounts button above to add one.")
            // A load that failed leaves the list as empty as one that succeeded
            // with nothing in it, so say which happened.
            const failure = page.chatList ? page.chatList.loadError : ""
            if (failure !== "")
                return qsTr("Couldn't load conversations.\n%1").arg(failure)
            if (!page.accountStatusKnown)
                return qsTr("Checking %1 …").arg(page.account)
            switch (page.connState) {
            case "connected":
                return qsTr("Connected as %1.\nNo conversations yet.").arg(page.account)
            case "auth-error":
                return qsTr("Authentication failed for %1.\nCheck the password.").arg(page.account)
            case "conn-error":
                return qsTr("Can't reach the server for %1.\nRetrying…").arg(page.account)
            case "disconnected":
            case "waiting":
                return qsTr("Reconnecting %1 …").arg(page.account)
            default: // starting, connecting, authenticating, binding
                return qsTr("Connecting %1 …").arg(page.account)
            }
        }
    }
}
