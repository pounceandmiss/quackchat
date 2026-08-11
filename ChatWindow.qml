pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quack

// A lightweight pop-out window holding a single conversation, no rail or list.
ApplicationWindow {
    id: win
    property string account: ""
    property string chatJid: ""
    property string chatName: ""
    property bool chatGroupchat: false

    width: 440
    height: 680
    minimumWidth: 320
    minimumHeight: 420
    visible: true
    title: chatName !== "" ? chatName : chatJid
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }
    // The search bar lives in the chat's own header, so a pop-out has one too.
    Shortcut { sequences: [StandardKey.Find]; onActivated: pane.openSearch() }

    ChatPage {
        id: pane
        anchors.fill: parent
        // Same safe-area padding as ShellWindow (zeros on desktop).
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        account: win.account
        chatJid: win.chatJid
        chatName: win.chatName
        chatGroupchat: win.chatGroupchat
        showBack: false  // nowhere to go back to
        canPopOut: false // cannot pop itself out again
    }
}
