pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Sign up for an account on a server that allows it (XEP-0077), in two steps:
// name the server, then answer whatever it asks. The Tk GUI's signup, and the
// same shape - the questions are the server's, so the second step is not a
// form this app knows the fields of.
//
// Registering does not create the account locally: tacky runs the sign-up on a
// throwaway connection and says only that the account now exists on the
// server. Adding it here is this sheet's last act, and only the answers say
// which account to add - which is why a server that never asks for a
// `username` leaves it to be added by hand.
SheetDialog {
    id: sheet
    objectName: "registerAccountSheet"

    title: qsTr("Create account")
    preferredWidth: 420
    height: Math.min(520, parent ? parent.height - 24 : 520)
    standardButtons: Dialog.Cancel

    readonly property bool busy: reg.state === RegistrationController.Connecting
                                 || reg.state === RegistrationController.Submitting

    RegistrationController {
        id: reg
        objectName: "registration"
        backend: App.backend
    }

    onAboutToShow: {
        reg.cancel()
        serverField.clear()
        serverField.forceActiveFocus()
    }
    // Covers Cancel, Esc and a click outside alike: the session is tacky's to
    // let go of, and it holds a connection until told.
    onClosed: reg.cancel()

    Connections {
        target: reg
        function onStateChanged() {
            // The password went in as an answer, so it is read back out of the
            // form rather than kept beside it.
            if (reg.state === RegistrationController.Registered
                    && reg.registeredJid !== "") {
                App.accounts.add(reg.registeredJid, reg.valueFor("password"))
                sheet.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Step one, and it stays put: which server this is a sign-up with is
        // worth keeping on screen, and changing it asks the new one afresh.
        Label {
            Layout.fillWidth: true
            //: The server a sign-up is being made with
            text: qsTr("Server")
            color: Theme.textDim
            font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: serverField
                objectName: "registerServer"
                Layout.fillWidth: true
                enabled: !sheet.busy
                placeholderText: qsTr("example.com")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: if (text.trim() !== "") reg.start(text.trim())
            }
            Button {
                objectName: "registerContinue"
                text: qsTr("Continue")
                enabled: !sheet.busy && serverField.text.trim() !== ""
                onClicked: reg.start(serverField.text.trim())
            }
        }

        // Step two: the server's own questions.
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: reg.hasForm
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: ThinScrollBar {}

            ColumnLayout {
                width: scroll.availableWidth
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    visible: reg.instructions !== ""
                    text: reg.instructions
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                DataForm {
                    objectName: "registrationForm"
                    Layout.fillWidth: true
                    // Nothing to answer while the answers are on their way.
                    enabled: !sheet.busy
                    fields: reg
                }
            }
        }

        // Standing in for the form until there is one.
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !reg.hasForm
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: {
                if (reg.state === RegistrationController.Connecting)
                    return qsTr("Asking %1 what it needs…").arg(reg.host)
                if (reg.state === RegistrationController.Registered)
                    return qsTr("The account was created, but the server never "
                              + "asked for a username, so it could not be added "
                              + "here.")
                return qsTr("Name a server to sign up with. Not every server "
                          + "hands out accounts.")
            }
        }

        Text {
            Layout.fillWidth: true
            visible: reg.error !== ""
            text: reg.error
            color: Theme.negative
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            visible: reg.hasForm
            spacing: 8

            // A rejected submit can have spent the form behind it - a CAPTCHA
            // in it has expired either way - so fetching a fresh one is its own
            // button. It keeps the answers already typed.
            Button {
                objectName: "registerRetry"
                text: qsTr("Refresh form")
                enabled: !sheet.busy
                onClicked: reg.retry()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "registerSubmit"
                text: reg.state === RegistrationController.Submitting
                      ? qsTr("Creating…") : qsTr("Create account")
                enabled: reg.complete && !sheet.busy
                onClicked: reg.submitForm()
            }
        }
    }
}
