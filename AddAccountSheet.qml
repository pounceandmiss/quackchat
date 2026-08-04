pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The add-account form: a JID + password, wired to App.accounts.add (which
// creates the account and enables it, i.e. signs in). Opened from the rail's ＋.
Dialog {
    id: sheet
    parent: Overlay.overlay
    modal: true
    title: "Add account"
    // 340 where it fits, else shrink to the window (narrow phone screens).
    width: Math.min(340, parent ? parent.width - 24 : 340)
    standardButtons: Dialog.Cancel | Dialog.Ok

    // Height of the window strip the virtual keyboard leaves clear. Android
    // overlays the keyboard instead of resizing the window, so centering in
    // the overlay would put the password field under the keys.
    readonly property real clearHeight: {
        if (!parent)
            return 0
        if (!Qt.inputMethod.visible) // qmllint disable missing-property
            return parent.height
        const kbTop = Qt.inputMethod.keyboardRectangle.y // qmllint disable missing-property
        return kbTop > 0 ? Math.min(kbTop, parent.height) : parent.height
    }
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: Math.max(12, Math.round((sheet.clearHeight - height) / 2))
    Behavior on y { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

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
