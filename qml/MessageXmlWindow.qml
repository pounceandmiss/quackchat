import QtQuick
import Quack

// The desktop home for MessageXmlPage. Not kept one-per-subject the way the
// keys window is: nothing here can be edited, so two side by side are worth
// having - that is how one stanza gets compared against another.
AppWindow {
    id: win
    objectName: "messageXmlWindow"
    property string xml: ""

    width: 620
    height: 480
    minimumWidth: 360
    minimumHeight: 260
    title: qsTr("Message XML")

    MessageXmlPage {
        anchors.fill: parent
        xml: win.xml
        showClose: false // the window's own close button is right there
        onDone: win.close()
    }
}
