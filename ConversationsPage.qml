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
                        text: "Chats"
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
                Layout.preferredHeight: 34
                placeholderText: "Search chats and messages"
                enabled: page.account !== ""
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                font.pixelSize: 13
                leftPadding: 10
                rightPadding: clearSearch.visible ? clearSearch.width + 6 : 10
                background: Rectangle {
                    radius: 8
                    color: Theme.field
                    border.width: 1
                    border.color: searchField.activeFocus ? Theme.accent
                                                          : Theme.hairline
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

    // Window-level actions, previously a global toolbar. The keyboard shortcuts
    // (Ctrl+N / Ctrl+T) live on the window; this menu is their mouse path.
    AppMenu {
        id: overflow
        objectName: "overflowMenu"
        width: 190
        MenuEntry {
            objectName: "newChatEntry"
            text: "New chat…"
            enabled: page.account !== ""
            onTriggered: newChatSheet.open()
        }
        MenuEntry {
            objectName: "joinRoomEntry"
            text: "Join room…"
            enabled: page.account !== ""
            onTriggered: joinRoomSheet.open()
        }
        // The Tk list's "Sort by" submenu, flattened: two entries with a tick
        // are the whole of it. Per window and not remembered, as it is there.
        MenuEntry {
            objectName: "sortRecentEntry"
            text: "Sort by activity"
            trailing: visibleChats.sortMode === ChatListFilter.Recent ? "✓" : ""
            onTriggered: visibleChats.sortMode = ChatListFilter.Recent
        }
        MenuEntry {
            objectName: "sortNameEntry"
            text: "Sort by name"
            trailing: visibleChats.sortMode === ChatListFilter.Name ? "✓" : ""
            onTriggered: visibleChats.sortMode = ChatListFilter.Name
        }
        // The Tk account window's View menu, which is a page of its own here:
        // flattened into this one it buried the actions it sat among.
        MenuEntry {
            objectName: "preferencesEntry"
            text: "Preferences…"
            onTriggered: page.openPreferences()
        }
        MenuEntry {
            text: "New window"
            trailing: "Ctrl+N"
            offered: !Theme.mobile // single-window platforms
            onTriggered: AppWindows.newShell(page.account)
        }
        MenuEntry {
            text: "Change theme"
            trailing: "Ctrl+T"
            onTriggered: Theme.cycle()
        }
        // The Tk list's Refresh: not a repaint but a re-ask, for when the
        // server and what we hold have drifted apart.
        MenuEntry {
            objectName: "refreshEntry"
            text: "Refresh"
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

        ScrollBar.vertical: ScrollBar {
            id: listScroll
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: Theme.textDim
                visible: listScroll.size < 1
                opacity: listScroll.pressed ? 0.8 : listScroll.active ? 0.5 : 0.25
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }

        // A heading over the only thing here says nothing; it earns its place
        // once the hits are underneath it.
        header: SectionLabel {
            width: listView.width
            text: "Chats"
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
                        text: row.unread > 99 ? "99+" : row.unread.toString()
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
            renamePrompt.prompt = "New name for " + jid + ":"
            renamePrompt.value = chatTitle
            renamePrompt.open()
        }
        onEditBookmark: {
            bookmarkPrompt.subject = jid
            bookmarkPrompt.prompt = "Bookmark name for " + jid + ":"
            bookmarkPrompt.value = chatTitle
            bookmarkPrompt.open()
        }
        onRemoveContact: {
            removeContactConfirm.subject = jid
            removeContactConfirm.message = "Remove " + jid + " from your contacts?"
            removeContactConfirm.open()
        }
        onRemoveBookmark: {
            removeBookmarkConfirm.subject = jid
            removeBookmarkConfirm.message = "Remove the bookmark for " + jid + "?"
            removeBookmarkConfirm.open()
        }
    }

    // Full-screen rather than centred: a page of settings is a screenful.
    Dialog {
        id: prefsSheet
        objectName: "preferencesSheet"
        parent: Overlay.overlay
        modal: true
        padding: 0
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0

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
        title: "Rename contact"
        onSubmitted: (text) => {
            if (page.chatList && text !== "" && text !== value)
                page.chatList.renameContact(subject, text)
        }
    }
    TextPromptDialog {
        id: bookmarkPrompt
        objectName: "bookmarkPrompt"
        title: "Edit bookmark"
        onSubmitted: (text) => {
            if (page.chatList && text !== "" && text !== value)
                page.chatList.renameBookmark(subject, text)
        }
    }
    ConfirmDialog {
        id: removeContactConfirm
        objectName: "removeContactConfirm"
        title: "Remove contact"
        onAccepted: if (page.chatList) page.chatList.removeContact(subject)
    }
    ConfirmDialog {
        id: removeBookmarkConfirm
        objectName: "removeBookmarkConfirm"
        title: "Remove bookmark"
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
                return "No account selected.\nUse the accounts button above to add one."
            // A load that failed leaves the list as empty as one that succeeded
            // with nothing in it, so say which happened.
            const failure = page.chatList ? page.chatList.loadError : ""
            if (failure !== "")
                return "Couldn't load conversations.\n" + failure
            switch (page.connState) {
            case "connected":
                return "Connected as " + page.account + ".\nNo conversations yet."
            case "auth-error":
                return "Authentication failed for " + page.account + ".\nCheck the password."
            case "conn-error":
                return "Can't reach the server for " + page.account + ".\nRetrying…"
            case "disconnected":
            case "waiting":
                return "Reconnecting " + page.account + " …"
            default: // starting, connecting, authenticating, binding
                return "Connecting " + page.account + " …"
            }
        }
    }
}
