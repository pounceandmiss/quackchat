import QtQuick
import Quack

// The desktop home for MucDetailsPage, and the room's counterpart to
// ContactDetailsWindow. One per room, kept by AppWindows: two windows on one
// room would each be moderating it, and each would be showing the other's
// kicks arriving as if from nowhere.
AppWindow {
    id: win
    objectName: "mucDetailsWindow"
    property string account: ""
    property string jid: ""
    property string name: ""

    width: 460
    height: 700
    minimumWidth: 360
    minimumHeight: 420
    title: qsTr("Room details — %1").arg(name !== "" ? name : jid)

    MucDetailsPage {
        anchors.fill: parent
        account: win.account
        jid: win.jid
        name: win.name
        shown: win.visible
        // Only rooms get this window, so asking for the chat as one is safe.
        omemo: {
            const s = App.chatFor(win.account, win.jid, true)
            return s ? s.omemo : null
        }
        showClose: false // the window's own close button is right there
        onDone: win.close()
        // Back to the room's chat, wherever it is shown, with them mentioned.
        onMentionRequested: (nick) => {
            const s = App.chatFor(win.account, win.jid, true)
            if (s)
                s.mention(nick)
            AppWindows.showChat(win.account, win.jid)
            win.close()
        }
        onPersonRequested: (jid, name) => AppWindows.contactDetails(win.account, jid, name, "")
    }
}
