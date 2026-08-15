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
            selectByMouse: true
            wrapMode: TextArea.NoWrap
            font.family: "monospace"
            font.pixelSize: 12
            background: null
        }
    }
}
