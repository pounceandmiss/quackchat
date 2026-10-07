import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Changes an account's password on the server: the Tk GUI's
// changepassworddialog. Change does not close it - the dialog waits for the
// server, closes on success and keeps a refusal in view, so the new password
// can be corrected and tried again.
SheetDialog {
    id: dlg
    objectName: "changePasswordDialog"

    property var settings: null
    readonly property bool busy: dlg.settings !== null && dlg.settings.changingPassword

    preferredWidth: 380
    title: qsTr("Change password")
    closePolicy: dlg.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    function change() {
        if (!dlg.busy && dlg.settings && newField.text !== "")
            dlg.settings.changeServerPassword(newField.text)
    }

    onAboutToShow: {
        newField.clear()
        revealBox.checked = false
        if (dlg.settings)
            dlg.settings.resetChangePassword()
        newField.forceActiveFocus()
    }

    Connections {
        target: dlg.settings
        function onServerPasswordChanged() { dlg.close() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: qsTr("Changes the password for %1 on the server. Quack signs in with the new one from now on; other clients will need it too.")
                  .arg(dlg.settings ? dlg.settings.account : "")
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        TextField {
            id: newField
            objectName: "newPasswordField"
            Layout.fillWidth: true
            enabled: !dlg.busy
            placeholderText: qsTr("New password")
            echoMode: revealBox.checked ? TextInput.Normal : TextInput.Password
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
            onTextChanged: if (dlg.settings) dlg.settings.resetChangePassword()
            onAccepted: dlg.change()
        }

        RevealBox {
            id: revealBox
            Layout.fillWidth: true
            enabled: !dlg.busy
        }

        Text {
            objectName: "changePasswordStatus"
            Layout.fillWidth: true
            visible: text !== ""
            text: dlg.busy ? qsTr("Changing…")
                : dlg.settings && dlg.settings.changePasswordError !== ""
                ? qsTr("Password not changed: %1").arg(dlg.settings.changePasswordError)
                : ""
            color: dlg.busy ? Theme.textDim : Theme.negative
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            enabled: !dlg.busy
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
        // An action rather than an accept, so the box stays open on the reply.
        Button {
            objectName: "changePasswordButton"
            text: qsTr("Change")
            enabled: !dlg.busy && newField.text !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: dlg.change()
        }
    }
}
