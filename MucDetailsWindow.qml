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
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
