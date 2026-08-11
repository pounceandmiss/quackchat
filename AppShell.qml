pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Wide window: rail | list | chat side by side. Narrow: collapses to the
// rail plus one column that shows the list, or the open chat with a back
// button. One instance per window.
Item {
    id: shell
    objectName: "appShell"
    property string initialAccount: ""
    property string currentAccount: initialAccount
    property string currentChatJid: ""
    property string currentChatName: ""
    property bool currentChatGroupchat: false

    readonly property bool wide: width >= 720

    // Fall back to the first available account when none is selected (or the
    // selected one disappears, e.g. after a remove).
    function ensureAccount() {
        if (currentAccount === "" || !App.accounts.contains(currentAccount))
            currentAccount = App.accounts.firstJid()
    }
    Component.onCompleted: ensureAccount()
    Connections {
        target: App.accounts
        function onCountChanged() { shell.ensureAccount() }
    }

    // The account-wide search stands in for the conversations list rather than
    // opening beside it, so it works the same in the wide and stacked layouts.
    // Searching inside one chat happens in the chat's own header.
    property bool searching: false

    function closeChat() {
        currentChatJid = ""
        currentChatName = ""
        currentChatGroupchat = false
    }

    function openSearch() {
        searching = true
        searchPane.focusInput()
    }

    // Where Ctrl+F lands: an open conversation is what you are most likely
    // looking through, and the whole account is what you mean when there is
    // none.
    function startFind() {
        if (currentChatJid !== "")
            chatPage.openSearch()
        else
            openSearch()
    }

    onCurrentAccountChanged: {
        closeChat()
        searching = false
    }

    // Android's system back arrives as a close request. Unwind one navigation
    // step instead: message selection first, then the open chat in the
    // stacked layout. Returns false when there's nothing left to pop.
    function handleBack() {
        if (chatPage.closeKeys())
            return true
        if (chatPage.closeSearch())
            return true
        if (chatPage.selectionMode) {
            chatPage.clearSelection()
            return true
        }
        // Search sits on top of the chat in the stacked layout, so it unwinds
        // first - the order things were opened in.
        if (searching) {
            searching = false
            return true
        }
        if (!wide && currentChatJid !== "") {
            closeChat()
            return true
        }
        return false
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AccountRail {
            Layout.fillHeight: true
            Layout.preferredWidth: 64
            // In the narrow layout an open chat takes the full window; the
            // rail steps aside and returns via the chat's back button.
            visible: shell.wide || shell.currentChatJid === ""
            currentAccount: shell.currentAccount
            onSelectAccount: (jid) => shell.currentAccount = jid
        }

        // Draggable divider; SplitView stores the dragged size in the list's
        // SplitView.preferredWidth.
        SplitView {
            id: split
            Layout.fillHeight: true
            Layout.fillWidth: true
            orientation: Qt.Horizontal

            // Visually the old 1px hairline; the containmentMask widens the
            // draggable strip well past it so it is grabbable by touch.
            handle: Rectangle {
                id: grip
                implicitWidth: 1
                color: Theme.hairline
                readonly property bool engaged: SplitHandle.hovered || SplitHandle.pressed
                // Widens to the right only. Handles sit above the panes, so a
                // centred strip eats presses on the list's scrollbar.
                containmentMask: Item {
                    width: 18
                    height: grip.height
                }
                // Always-on grab nub, for touch discoverability.
                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: 36
                    radius: 2
                    color: Theme.textDim
                    opacity: grip.engaged ? 0 : 0.35
                }
                // Accent highlight along the divider while dragging.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 3
                    height: parent.height
                    color: Theme.accent
                    opacity: grip.engaged ? (grip.SplitHandle.pressed ? 0.8 : 0.45) : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }
            }

            ConversationsPage {
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 220
                // Cap the list at half the split, but only while it shares the
                // split with the chat; alone (narrow layout) it must fill.
                SplitView.maximumWidth: shell.wide ? split.width * 0.5 : split.width
                // Hidden (and thus excluded from the split) when a chat is open
                // in the narrow layout; as the only visible pane it auto-fills.
                visible: !shell.searching && (shell.wide || shell.currentChatJid === "")
                account: shell.currentAccount
                onOpenChat: (jid, name, groupchat) => {
                    shell.currentChatJid = jid
                    shell.currentChatName = name
                    shell.currentChatGroupchat = groupchat
                }
                onPopOutChat: (jid, name, groupchat) => {
                    if (!Theme.mobile)
                        AppWindows.popOut(shell.currentAccount, jid, name, groupchat)
                }
                onStartSearch: shell.openSearch()
            }

            SearchPage {
                id: searchPane
                SplitView.preferredWidth: 320
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: shell.wide ? split.width * 0.5 : split.width
                // Stacked, it takes the column from the open chat; side by side
                // it takes it from the conversations list.
                visible: shell.searching
                account: shell.currentAccount
                onClosed: shell.searching = false
                // The hit names its own chat, which need not be the open one.
                // Whether that chat is a room is the chat list's answer, not a
                // reading of the JID.
                onOpenHit: (jid, ts, matches) => {
                    if (jid !== shell.currentChatJid) {
                        const entry = App.chatListFor(shell.currentAccount).entryFor(jid)
                        shell.currentChatJid = jid
                        shell.currentChatName = entry.name !== undefined ? entry.name : ""
                        shell.currentChatGroupchat = entry.groupchat === true
                    }
                    chatPage.jumpTo(ts, matches)
                    // Stacked, the results are covering the message they point
                    // at; side by side both are on screen and the list stays.
                    if (!shell.wide)
                        shell.searching = false
                }
            }

            ChatPage {
                id: chatPage
                SplitView.fillWidth: true
                SplitView.minimumWidth: 280
                visible: shell.wide || (shell.currentChatJid !== "" && !shell.searching)
                account: shell.currentAccount
                chatJid: shell.currentChatJid
                chatName: shell.currentChatName
                chatGroupchat: shell.currentChatGroupchat
                showBack: !shell.wide
                canPopOut: shell.wide && !Theme.mobile
                onBack: shell.closeChat()
                onPopOut: {
                    AppWindows.popOut(shell.currentAccount, shell.currentChatJid,
                                      shell.currentChatName,
                                      shell.currentChatGroupchat)
                    shell.closeChat()
                }
            }
        }
    }
}
