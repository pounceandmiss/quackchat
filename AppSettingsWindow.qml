import QtQuick
import QtQuick.Controls
import Quack

// The desktop home for AppSettingsPage. AppWindows keeps the one and raises it.
ApplicationWindow {
    id: win

    width: 460
    height: 700
    minimumWidth: 360
    minimumHeight: 420
    visible: true
    title: "Preferences"
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }

    AppSettingsPage {
        anchors.fill: parent
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
