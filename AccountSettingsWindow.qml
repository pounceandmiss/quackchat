import QtQuick
import Quack

// The desktop home for AccountSettingsPage. One per account, kept by
// AppWindows so a second request raises the open one instead of stacking a
// duplicate over it.
AppWindow {
    id: win
    property string account: ""

    width: 520
    height: 760
    minimumWidth: 380
    minimumHeight: 480
    title: qsTr("Account details — %1").arg(account)

    AccountSettingsPage {
        anchors.fill: parent
        account: win.account
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
