import QtQuick
import Quack

// The desktop home for AppSettingsPage. AppWindows keeps the one and raises it.
AppWindow {
    id: win

    width: 460
    height: 700
    minimumWidth: 360
    minimumHeight: 420
    title: qsTr("Preferences")

    AppSettingsPage {
        anchors.fill: parent
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
