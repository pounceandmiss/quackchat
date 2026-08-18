pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One message's stored stanza, laid out by the model. Reached from the bubble's
// "View XML", and hosted the same two ways the keys page is: a window on
// desktop, a full-screen sheet on mobile.
Page {
    id: page
    objectName: "messageXmlPage"

    // The laid-out stanza. Empty for a message that never had one built.
    property string xml: ""
    property bool showClose: true
    // Read with a finger rather than a pointer, which is what the TextArea
    // below hands the drag over for. The platform is as much as the page can
    // tell from here.
    property bool touch: Theme.mobile
    readonly property bool hasXml: xml !== ""
    signal done

    background: Rectangle { color: Theme.background }

    header: PageHeader {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: page.showClose ? 4 : 16
            anchors.rightMargin: 8
            spacing: 4
            IconButton {
                iconPath: Icons.arrowBack
                Accessible.name: qsTr("Back")
                visible: page.showClose
                glyphColor: Theme.textDim
                onClicked: page.done()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: "Message XML"
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    // tacky records this in the clear even for a message that
                    // went out encrypted, so say so rather than let it pass
                    // for the bytes on the wire.
                    text: "tacky's stored record, not the wire"
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            Button {
                text: "Copy"
                enabled: page.hasXml
                onClicked: Clipboard.setText(page.xml)
            }
        }
    }

    // Unwrapped and scrolled both ways: the line breaks are the structure, and
    // folding a long one would read as a break the stanza does not have.
    ScrollView {
        objectName: "xmlScroll"
        anchors.fill: parent
        anchors.margins: 12
        clip: true
        ScrollBar.vertical: ThinScrollBar {}
        ScrollBar.horizontal: ThinScrollBar {}

        TextArea {
            objectName: "xmlText"
            text: page.hasXml ? page.xml
                              : "No stanza recorded for this message."
            color: page.hasXml ? Theme.textPrimary : Theme.textDim
            readOnly: true
            // A finger drag over a TextArea that selects by mouse is taken
            // for a selection, so it never reaches the flick underneath - and
            // it selects nothing anyway, touch selection going through the
            // handles. The press takes the focus too, which on Android brings
            // the keyboard up over a page with nothing to type into. So to a
            // finger this is text to scroll, and Copy is in the header.
            selectByMouse: !page.touch
            activeFocusOnPress: !page.touch
            wrapMode: TextArea.NoWrap
            font.family: "monospace"
            font.pixelSize: 12
            background: null
        }
    }
}
