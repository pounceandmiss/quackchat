import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The chat list's row menu, from the Tk list's pair of context menus: a room
// gets the bookmark verbs (join, force join, edit, remove bookmark), anything
// else gets the contact verbs (call, rename, remove). `groupchat` picks between
// them, exactly as it does there.
//
// One instance per list rather than one per delegate: the menu is a windowful
// of scenery for a row that is nearly always just clicked, and the list can run
// long. openFor() loads it from the row's entry before it pops up.
Menu {
    id: menu
    objectName: "chatRowMenu"
    width: 220

    property string jid: ""
    property string chatTitle: ""
    // The chatlist entry's `source`: roster, bookmarks, or free (has history but
    // is in neither). A free chat has no roster item to rename or remove, so it
    // is offered the way in instead.
    property string source: ""
    property bool groupchat: false
    property bool autojoin: false
    property string roomState: ""
    property string roomReason: ""
    // Popping a chat into its own window is a desktop-only idea.
    property bool canPopOut: true

    signal openChat()
    signal popOutChat()
    signal startCall()
    signal addContact()
    signal renameContact()
    signal removeContact()
    signal joinRoom()
    signal leaveRoom()
    signal forceJoin()
    signal editBookmark()
    signal removeBookmark()
    signal refreshAvatar()
    signal copyJid()

    // `entry` is the row's chatlist entry, verbatim. Every field is optional: a
    // free chat carries no bookmark fields and a 1:1 chat no room state.
    function openFor(entry) {
        menu.jid = entry.jid ?? ""
        // The row's own title rule: an unnamed chat goes by its JID.
        menu.chatTitle = entry.name ? entry.name : menu.jid
        menu.source = entry.source ?? ""
        menu.groupchat = entry.groupchat === true
        menu.autojoin = entry.autojoin === true
        menu.roomState = entry.room_state ?? ""
        menu.roomReason = entry.room_reason ?? ""
        menu.popup()
    }

    // What the Tk bookmark menu says under the JID. States the user can do
    // nothing about (joined, idle) say nothing at all.
    readonly property string statusLine: {
        if (!menu.groupchat)
            return ""
        switch (menu.roomState) {
        case "error":
            return menu.roomReason !== "" ? "Join failed: " + menu.roomReason
                                          : "Join failed"
        case "joining":
            return "Joining…"
        case "disconnected":
            return "Not connected"
        default:
            return ""
        }
    }

    background: Rectangle {
        color: Theme.surface
        radius: 10
        border.color: Theme.hairline
    }

    // An entry a row has no use for collapses rather than merely hiding: an
    // invisible one at full height would hold a blank slot in the column.
    component MenuEntry: MenuItem {
        id: mi
        height: visible ? 40 : 0
        property string trailing: ""
        property color labelColor: Theme.textPrimary
        contentItem: RowLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: mi.text
                color: mi.labelColor
                font.pixelSize: 14
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            Text {
                text: mi.trailing
                color: Theme.textDim
                font.pixelSize: 13
                rightPadding: 8
            }
        }
        background: Rectangle {
            color: mi.highlighted ? Theme.menuHover : "transparent"
            radius: 6
        }
    }

    // A label, not an action - the JID this menu is about, and what the room is
    // doing. Collapses when empty, for the same reason MenuEntry does.
    component MenuLabel: MenuItem {
        id: ml
        enabled: false
        height: visible ? 26 : 0
        contentItem: Text {
            text: ml.text
            color: Theme.textDim
            font.pixelSize: 12
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
        background: Item {}
    }

    component Rule: MenuSeparator {
        padding: 4
        contentItem: Rectangle {
            implicitHeight: 1
            color: Theme.hairline
        }
    }

    MenuLabel { text: menu.chatTitle }
    MenuLabel {
        objectName: "roomStatusLine"
        text: menu.statusLine
        visible: menu.statusLine !== ""
    }
    Rule {}

    MenuEntry {
        text: "Open chat"
        onTriggered: menu.openChat()
    }
    MenuEntry {
        objectName: "popOutEntry"
        text: "Open in new window"
        visible: menu.canPopOut
        onTriggered: menu.popOutChat()
    }
    // A room's calls are its occupants', not the room's.
    MenuEntry {
        objectName: "startCallEntry"
        text: "Start call"
        visible: !menu.groupchat
        onTriggered: menu.startCall()
    }

    // The Tk list's "Join" tick: membership, not attendance. Ticking joins the
    // room and remembers it; unticking leaves and forgets it.
    MenuEntry {
        objectName: "joinEntry"
        text: "Join"
        trailing: menu.autojoin ? "✓" : ""
        visible: menu.groupchat
        onTriggered: menu.autojoin ? menu.leaveRoom() : menu.joinRoom()
    }
    // Re-attempts a room we are a member of but have been dropped from (an IRC
    // gateway disconnect, say) without touching that membership.
    MenuEntry {
        objectName: "forceJoinEntry"
        text: "Force join request"
        visible: menu.groupchat
        onTriggered: menu.forceJoin()
    }

    Rule {}

    MenuEntry {
        objectName: "addContactEntry"
        text: "Add to contacts"
        // Only a chat that is in neither the roster nor the bookmarks: the rest
        // are already somewhere this would put them.
        visible: !menu.groupchat && menu.source === "free"
        onTriggered: menu.addContact()
    }
    MenuEntry {
        objectName: "renameEntry"
        text: menu.groupchat ? "Edit name…" : "Rename…"
        visible: menu.groupchat || menu.source === "roster"
        onTriggered: menu.groupchat ? menu.editBookmark() : menu.renameContact()
    }
    MenuEntry {
        objectName: "removeEntry"
        text: menu.groupchat ? "Remove bookmark…" : "Remove…"
        labelColor: Theme.negative
        visible: menu.groupchat || menu.source === "roster"
        onTriggered: menu.groupchat ? menu.removeBookmark() : menu.removeContact()
    }

    Rule {}

    MenuEntry {
        text: "Refresh avatar"
        onTriggered: menu.refreshAvatar()
    }
    MenuEntry {
        objectName: "copyJidEntry"
        text: "Copy JID"
        onTriggered: menu.copyJid()
    }
}
