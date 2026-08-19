pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The add-account form: a JID + password, wired to App.accounts.add (which
// creates the account and enables it, i.e. signs in). Opened from the rail's ＋.
//
// Signing in is the whole of it; someone with no account yet is handed on to
// RegisterAccountSheet, which is where a server is asked to make one.
SheetDialog {
    id: sheet
    title: qsTr("Add account")
    standardButtons: Dialog.Cancel | Dialog.Ok

    // Handled by whoever hosts this sheet: the sign-up is a sheet of its own.
    signal createAccount

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
            text: qsTr("Jabber ID")
            color: Theme.textDim
            font.pixelSize: 12
        }
        TextField {
            id: jidField
            Layout.fillWidth: true
            placeholderText: qsTr("you@example.com")
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            onAccepted: passwordField.forceActiveFocus()
        }

        Label {
            text: qsTr("Password")
            color: Theme.textDim
            font.pixelSize: 12
        }
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: qsTr("password")
            echoMode: TextInput.Password
            onAccepted: if (sheet.jidValid) sheet.accept()
        }

        Button {
            objectName: "createAccountLink"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            flat: true
            text: qsTr("Create a new account")
            onClicked: sheet.createAccount()
        }
    }
}
