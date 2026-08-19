import QtQuick
import QtQuick.Controls
import Quack

// The desktop home for AccountSettingsPage. One per account, kept by
// AppWindows so a second request raises the open one instead of stacking a
// duplicate over it.
ApplicationWindow {
    id: win
    property string account: ""

    width: 520
    height: 760
    minimumWidth: 380
    minimumHeight: 480
    visible: true
    title: qsTr("Account details — %1").arg(account)
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }

    AccountSettingsPage {
        anchors.fill: parent
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        account: win.account
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
