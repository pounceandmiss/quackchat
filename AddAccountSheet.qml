pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The add-account form: a JID + password, wired to App.accounts.add (which
// creates the account and enables it, i.e. signs in). Opened from the rail's ＋.
SheetDialog {
    id: sheet
    title: "Add account"
    standardButtons: Dialog.Cancel | Dialog.Ok

    // Enable OK only once the JID looks like user@domain.
    readonly property bool jidValid: /^[^@\s]+@[^@\s]+$/.test(jidField.text.trim())

    onAboutToShow: {
        jidField.clear()
        passwordField.clear()
        jidField.forceActiveFocus()
    }
    onAccepted: App.accounts.add(jidField.text.trim(), passwordField.text)

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
            Layout.fillWidth: true
            placeholderText: "you@example.com"
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            onAccepted: passwordField.forceActiveFocus()
        }

        Label {
            text: "Password"
            color: Theme.textDim
            font.pixelSize: 12
        }
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "password"
            echoMode: TextInput.Password
            onAccepted: if (sheet.jidValid) sheet.accept()
        }
    }
}
