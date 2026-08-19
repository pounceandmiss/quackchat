import QtQuick
import QtQuick.Controls
import Quack

// The chat list's row menu, from the Tk list's pair of context menus: a room
// gets the bookmark verbs (join, force join, edit, remove bookmark), anything
// else gets the contact verbs (call, rename, remove). `groupchat` picks between
// them, exactly as it does there.
//
// One instance per list rather than one per delegate: the menu is a windowful
// of scenery for a row that is nearly always just clicked, and the list can run
// long. openFor() loads it from the row's entry before it pops up.
AppMenu {
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
            return menu.roomReason !== ""
                 ? qsTr("Join failed: %1").arg(menu.roomReason)
                 : qsTr("Join failed")
        case "joining":
            return qsTr("Joining…")
        case "disconnected":
            return qsTr("Not connected")
        default:
            return ""
        }
    }

    // A label, not an action - the JID this menu is about, and what the room is
    // doing. Collapses when empty, the way a MenuEntry does.
    component MenuLabel: MenuItem {
        id: ml
        property bool offered: true
        enabled: false
        visible: ml.offered
        height: ml.offered ? 26 : 0
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
        offered: menu.statusLine !== ""
    }
    Rule {}

    MenuEntry {
        text: qsTr("Open chat")
        onTriggered: menu.openChat()
    }
    MenuEntry {
        objectName: "popOutEntry"
        text: qsTr("Open in new window")
        offered: menu.canPopOut
        onTriggered: menu.popOutChat()
    }
    // A room's calls are its occupants', not the room's.
    MenuEntry {
        objectName: "startCallEntry"
        text: qsTr("Start call")
        offered: !menu.groupchat
        onTriggered: menu.startCall()
    }

    // The Tk list's "Join" tick: membership, not attendance. Ticking joins the
    // room and remembers it; unticking leaves and forgets it.
    MenuEntry {
        objectName: "joinEntry"
        //: Bookmark membership, ticked when the room is joined automatically
        text: qsTr("Join")
        trailing: menu.autojoin ? "✓" : ""
        offered: menu.groupchat
        onTriggered: menu.autojoin ? menu.leaveRoom() : menu.joinRoom()
    }
    // Re-attempts a room we are a member of but have been dropped from (an IRC
    // gateway disconnect, say) without touching that membership.
    MenuEntry {
        objectName: "forceJoinEntry"
        text: qsTr("Force join request")
        offered: menu.groupchat
        onTriggered: menu.forceJoin()
    }

    Rule {}

    MenuEntry {
        objectName: "addContactEntry"
        text: qsTr("Add to contacts")
        // Only a chat that is in neither the roster nor the bookmarks: the rest
        // are already somewhere this would put them.
        offered: !menu.groupchat && menu.source === "free"
        onTriggered: menu.addContact()
    }
    MenuEntry {
        objectName: "renameEntry"
        text: menu.groupchat ? qsTr("Edit name…") : qsTr("Rename…")
        offered: menu.groupchat || menu.source === "roster"
        onTriggered: menu.groupchat ? menu.editBookmark() : menu.renameContact()
    }
    MenuEntry {
        objectName: "removeEntry"
        text: menu.groupchat ? qsTr("Remove bookmark…") : qsTr("Remove…")
        labelColor: Theme.negative
        offered: menu.groupchat || menu.source === "roster"
        onTriggered: menu.groupchat ? menu.removeBookmark() : menu.removeContact()
    }

    Rule {}

    MenuEntry {
        text: qsTr("Refresh avatar")
        onTriggered: menu.refreshAvatar()
    }
    MenuEntry {
        objectName: "copyJidEntry"
        text: qsTr("Copy JID")
        onTriggered: menu.copyJid()
    }
}
