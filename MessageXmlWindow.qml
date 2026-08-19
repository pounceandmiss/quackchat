import QtQuick
import QtQuick.Controls
import Quack

// The desktop home for MessageXmlPage. Not kept one-per-subject the way the
// keys window is: nothing here can be edited, so two side by side are worth
// having - that is how one stanza gets compared against another.
ApplicationWindow {
    id: win
    objectName: "messageXmlWindow"
    property string xml: ""

    width: 620
    height: 480
    minimumWidth: 360
    minimumHeight: 260
    visible: true
    title: qsTr("Message XML")
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }

    MessageXmlPage {
        anchors.fill: parent
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        xml: win.xml
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
