import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The startup gate for local storage encryption, and the only usable thing on
// screen while it stands: with the store locked or a migration pending, tacky
// has installed no other module, so the shell behind it is empty.
//
// The Tk client shows this before any window exists; here the shell is already
// up, so it is a modal dialog over it. Every shell window carries one and they
// all read App.storage, so resolving any of them closes the rest.
SheetDialog {
    id: gate
    objectName: "storageGate"

    // What the passphrase field holds, kept here because the field itself is
    // absent for a decrypt.
    property string entry: ""

    readonly property string status: App.storage.status
    readonly property bool unlocking: gate.status === "locked"
    readonly property bool encrypting: gate.status === "pending-encrypt"
    // Only encrypt sets a passphrase. A decrypt already has the one in memory
    // from the unlock that revealed it.
    readonly property bool wantsPassphrase: gate.unlocking || gate.encrypting
    // A migration can be abandoned - the store stays as it is, unmigrated.
    // A locked store cannot: there is nothing else the app can do.
    readonly property bool cancellable: !gate.unlocking

    title: {
        if (gate.unlocking)
            return qsTr("Unlock local storage")
        if (gate.encrypting)
            return qsTr("Enable encryption")
        return qsTr("Remove encryption")
    }

    preferredWidth: 380
    // Neither Escape nor a click outside: the buttons are the only way through.
    closePolicy: Popup.NoAutoClose
    visible: App.storage.gateActive

    onVisibleChanged: if (gate.visible) {
        gate.entry = ""
        if (gate.wantsPassphrase)
            passField.forceActiveFocus()
    }

    function submit() {
        if (gate.wantsPassphrase && gate.entry === "")
            return
        if (gate.unlocking)
            App.storage.unlock(gate.entry)
        else if (gate.encrypting)
            App.storage.encrypt(gate.entry)
        else
            App.storage.decrypt()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // Not a footnote: tacky keeps no escrow, so this is the whole of the
        // informed part of choosing a passphrase.
        Text {
            objectName: "storageGateWarning"
            Layout.fillWidth: true
            visible: gate.encrypting
            text: qsTr("There is no way to recover your data if you forget this passphrase.")
            color: Theme.textPrimary
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        Text {
            objectName: "storageGateBlurb"
            Layout.fillWidth: true
            text: {
                if (gate.unlocking)
                    return qsTr("Enter your passphrase to unlock local storage.")
                if (gate.encrypting)
                    return qsTr("Everything stored on this device is about to be rewritten, encrypted. It can take a while.")
                return qsTr("Local storage will be written back out as plaintext.")
            }
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        TextField {
            id: passField
            objectName: "storagePassphrase"
            Layout.fillWidth: true
            visible: gate.wantsPassphrase
            enabled: !App.storage.busy
            placeholderText: qsTr("passphrase")
            echoMode: revealBox.checked ? TextInput.Normal : TextInput.Password
            text: gate.entry
            onTextChanged: gate.entry = text
            onAccepted: gate.submit()
        }

        CheckBox {
            id: revealBox
            objectName: "storageReveal"
            Layout.fillWidth: true
            visible: gate.wantsPassphrase
            padding: 0
            // The style centres its indicator when the control has no text of
            // its own, so set it even though contentItem draws it.
            text: qsTr("Show passphrase")
            contentItem: Text {
                text: revealBox.text
                color: Theme.textDim
                font.pixelSize: 12
                leftPadding: revealBox.indicator.width + 8
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            objectName: "storageGateError"
            Layout.fillWidth: true
            visible: App.storage.error !== ""
            text: App.storage.error
            color: Theme.negative
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        // tacky reports progress per staged file. Indeterminate until the first
        // <MigrateProgress> names a total, since the file scan comes first.
        ProgressBar {
            objectName: "storageProgress"
            Layout.fillWidth: true
            visible: App.storage.busy && !gate.unlocking
            indeterminate: App.storage.progressTotal <= 0
            from: 0
            to: Math.max(1, App.storage.progressTotal)
            value: App.storage.progressDone
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                objectName: "storageGateCancel"
                visible: gate.cancellable
                enabled: !App.storage.busy
                text: qsTr("Cancel")
                onClicked: App.storage.cancelPending()
            }
            Button {
                objectName: "storageGateGo"
                enabled: !App.storage.busy
                       && (!gate.wantsPassphrase || gate.entry !== "")
                text: {
                    // A failed migration is worth another try in place: nothing
                    // live was touched, so retrying starts from scratch.
                    if (App.storage.error !== "" && !gate.unlocking)
                        return qsTr("Retry")
                    if (gate.unlocking)
                        return qsTr("Unlock")
                    if (gate.encrypting)
                        return qsTr("Encrypt")
                    return qsTr("Remove encryption")
                }
                onClicked: gate.submit()
            }
        }
    }
}
