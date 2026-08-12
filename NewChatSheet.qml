import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Start a chat with a JID that has no row yet: the Tk GUI's newchatdialog, down
// to the "add to my contacts" tick. Adding is offered rather than assumed - a
// one-off reply to a stranger should not put them in the roster - but it is on
// by default, which is what starting a chat with someone usually means.
SheetDialog {
    id: sheet
    objectName: "newChatSheet"

    // The chat to open. `name` is only the roster name to store; the row that
    // appears afterwards carries it back.
    signal startChat(string jid, string name, bool addToContacts)

    title: "New chat"
    standardButtons: Dialog.Cancel | Dialog.Ok

    // Bare, so the chat we open and the row that answers agree on the key.
    readonly property string targetJid: Jid.bare(jidField.text)
    readonly property bool jidValid: Jid.plausible(targetJid)

    onAboutToShow: {
        jidField.clear()
        nameField.clear()
        addBox.checked = true
        jidField.forceActiveFocus()
    }
    onAccepted: sheet.startChat(sheet.targetJid, nameField.text.trim(),
                                addBox.checked)

    Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(() => sheet.jidValid)

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            text: "Jabber ID"
            color: Theme.textDim
            font.pixelSize: 12
        }
        TextField {
            id: jidField
            objectName: "newChatJid"
            Layout.fillWidth: true
            placeholderText: "someone@example.com"
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            onAccepted: nameField.forceActiveFocus()
        }

        Label {
            text: "Name (optional)"
            color: Theme.textDim
            font.pixelSize: 12
        }
        TextField {
            id: nameField
            objectName: "newChatName"
            Layout.fillWidth: true
            placeholderText: "what to call them"
            onAccepted: if (sheet.jidValid) sheet.accept()
        }

        CheckBox {
            id: addBox
            objectName: "newChatAddContact"
            checked: true
            text: "Add to my contacts"
            contentItem: Text {
                text: addBox.text
                color: Theme.textPrimary
                font.pixelSize: 14
                verticalAlignment: Text.AlignVCenter
                leftPadding: addBox.indicator.width + addBox.spacing
            }
        }
    }
}
