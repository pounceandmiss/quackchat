import QtQuick
import Quack

// What can be done about one person in a room, for a member in the room's
// details or a message's sender. Loaded from the person before it pops up - a
// MucRoomModel row, or what its requestPerson answered, which use the same
// names - the way the chat list's menu is, rather than one menu per delegate.
//
// Anything that depends on the host - a sheet or a window, the room page's
// confirmations - goes out as a signal for the page to act on.
AppMenu {
    id: personMenu
    objectName: "personMenu"
    width: 240

    // The room page's moderation entries, from the person's caps. Off in a
    // chat, which offers only the person.
    property bool moderation: false

    property string nick: ""
    property string realJid: ""
    property bool self: false
    property var caps: ({})

    signal detailsRequested()
    signal conversationRequested()
    signal copyRequested()
    // kick, ban, make_moderator, grant_voice, revoke_voice, grant_membership,
    // revoke_membership: the caps' own names.
    signal moderate(string what)

    function load(who) {
        personMenu.nick = who.nick ?? ""
        personMenu.realJid = who.realJid ?? ""
        personMenu.self = who.self === true
        personMenu.caps = who.caps ?? ({})
    }
    function can(what) { return personMenu.moderation && personMenu.caps[what] === true }

    // Our own account's page is a window of its own on desktop; on a phone it
    // lives in the account rail, out of reach from here.
    MenuEntry {
        objectName: "personDetailsEntry"
        text: personMenu.self ? qsTr("Account details") : qsTr("Contact details")
        offered: personMenu.realJid !== "" && !(personMenu.self && Theme.mobile)
        onTriggered: personMenu.detailsRequested()
    }
    MenuEntry {
        objectName: "startConversationEntry"
        text: qsTr("Start conversation")
        offered: personMenu.realJid !== "" && !personMenu.self
        onTriggered: personMenu.conversationRequested()
    }
    MenuEntry {
        objectName: "copyOccupantJidEntry"
        text: qsTr("Copy address")
        offered: personMenu.realJid !== ""
        onTriggered: personMenu.copyRequested()
    }

    MenuEntry {
        objectName: "kickEntry"
        text: qsTr("Kick…")
        offered: personMenu.can("kick")
        onTriggered: personMenu.moderate("kick")
    }
    MenuEntry {
        objectName: "banEntry"
        text: qsTr("Ban…")
        labelColor: Theme.negative
        offered: personMenu.can("ban")
        onTriggered: personMenu.moderate("ban")
    }
    MenuEntry {
        objectName: "makeModeratorEntry"
        text: qsTr("Make moderator")
        offered: personMenu.can("make_moderator")
        onTriggered: personMenu.moderate("make_moderator")
    }
    MenuEntry {
        objectName: "grantVoiceEntry"
        text: qsTr("Grant voice")
        offered: personMenu.can("grant_voice")
        onTriggered: personMenu.moderate("grant_voice")
    }
    MenuEntry {
        objectName: "revokeVoiceEntry"
        text: qsTr("Revoke voice")
        offered: personMenu.can("revoke_voice")
        onTriggered: personMenu.moderate("revoke_voice")
    }
    MenuEntry {
        objectName: "grantMembershipEntry"
        text: qsTr("Grant membership")
        offered: personMenu.can("grant_membership")
        onTriggered: personMenu.moderate("grant_membership")
    }
    MenuEntry {
        objectName: "revokeMembershipEntry"
        text: qsTr("Revoke membership")
        offered: personMenu.can("revoke_membership")
        onTriggered: personMenu.moderate("revoke_membership")
    }
}
