import QtQuick
import QtQuick.Controls
import Quack

// The desktop home for ContactDetailsPage. One per contact, kept by AppWindows
// so a second request raises the open one rather than stacking another view of
// the same trust state over it.
ApplicationWindow {
    id: win
    objectName: "contactDetailsWindow"
    property string account: ""
    property string jid: ""
    property string name: ""

    width: 520
    height: 720
    minimumWidth: 380
    minimumHeight: 420
    visible: true
    title: qsTr("Contact details — %1").arg(name !== "" ? name : jid)
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }

    ContactDetailsPage {
        anchors.fill: parent
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        account: win.account
        jid: win.jid
        name: win.name
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
