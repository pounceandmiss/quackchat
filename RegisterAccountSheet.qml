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
// server. Adding it here is this sheet's last act, and only it can be done
// from what the server asked - which is why a server that never asks for a
// `username` leaves the account to be added by hand.
SheetDialog {
    id: sheet
    objectName: "registerAccountSheet"

    title: "Create account"
    preferredWidth: 420
    height: Math.min(520, parent ? parent.height - 24 : 520)
    standardButtons: Dialog.Cancel

    readonly property bool haveForm: reg.hasForm
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
            if (reg.state !== RegistrationController.Registered)
                return
            // The password went in as an answer, so it is read back from the
            // form rather than kept beside it.
            if (reg.registeredJid !== "") {
                App.accounts.add(reg.registeredJid, reg.valueFor("password"))
                sheet.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Step one: which server. Kept visible behind the form so the answer
        // to "where am I signing up" stays on screen.
        Label {
            Layout.fillWidth: true
            text: "Server"
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
                enabled: !sheet.haveForm && !sheet.busy
                placeholderText: "example.com"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: if (text.trim() !== "") reg.start(text.trim())
            }
            Button {
                objectName: "registerContinue"
                text: sheet.haveForm ? "Change" : "Continue"
                enabled: sheet.haveForm
                         || (!sheet.busy && serverField.text.trim() !== "")
                // "Change" drops the form and the session behind it, back to an
                // empty server box.
                onClicked: {
                    if (sheet.haveForm) {
                        reg.cancel()
                        serverField.forceActiveFocus()
                    } else {
                        reg.start(serverField.text.trim())
                    }
                }
            }
        }

        // Step two: the server's own questions.
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: sheet.haveForm
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
                    fields: reg
                }
            }
        }

        // Standing in for the form until there is one.
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !sheet.haveForm
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: {
                if (reg.state === RegistrationController.Connecting)
                    return "Asking " + reg.host + " what it needs…"
                if (reg.state === RegistrationController.Registered)
                    return "The account was created on " + reg.host
                         + ", but the server never asked for a username, so it "
                         + "could not be added here. Add it with Sign in."
                return "Type the server to sign up with. Not every server "
                     + "allows it, and some ask you to register on their website "
                     + "instead."
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
            visible: sheet.haveForm
            spacing: 8

            // A failed submit can have spent the form behind it - a CAPTCHA in
            // it has expired either way - so fetching a fresh one is its own
            // button, and it keeps the answers already typed.
            Button {
                objectName: "registerRetry"
                text: "New form"
                enabled: !sheet.busy
                onClicked: reg.retry()
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "registerSubmit"
                text: reg.state === RegistrationController.Submitting
                      ? "Creating…" : "Create account"
                enabled: reg.complete && !sheet.busy
                onClicked: reg.submitForm()
            }
        }
    }
}
