pragma ComponentBehavior: Bound

import QtQuick
import Quack

// A lightweight pop-out window holding a single conversation, no rail or list.
AppWindow {
    id: win
    property string account: ""
    property string chatJid: ""
    property string chatName: ""
    property bool chatGroupchat: false

    width: 440
    height: 680
    minimumWidth: 320
    minimumHeight: 420
    title: chatName !== "" ? chatName : chatJid

    // The search bar lives in the chat's own header, so a pop-out has one too.
    Shortcut { sequences: [StandardKey.Find]; onActivated: pane.openSearch() }

    ChatPage {
        id: pane
        anchors.fill: parent
        account: win.account
        chatJid: win.chatJid
        chatName: win.chatName
        chatGroupchat: win.chatGroupchat
        showBack: false  // nowhere to go back to
        canPopOut: false // cannot pop itself out again
    }

    // A pop-out is somewhere you type: it says a dead backend as loudly as the
    // shell does.
    edgeToEdge: BackendNotice {}
}
