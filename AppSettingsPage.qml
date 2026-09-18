pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The preferences that belong to the app rather than to an account. Hosted by
// AppSettingsWindow on desktop and as a full-screen sheet on mobile, so it
// carries its own header. Each control writes as it is picked, so there is no
// Save.
Page {
    id: page
    objectName: "appSettingsPage"

    property bool showClose: true
    signal done

    background: Rectangle { color: Theme.background }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
    }

    // One choice out of a group. The dot is bound to the setting rather than
    // toggled by the click, so a write that never lands leaves the row where
    // it was.
    component OptionRow: ItemDelegate {
        id: opt
        property bool selected: false

        Layout.fillWidth: true
        implicitHeight: 36
        padding: 0
        Accessible.role: Accessible.RadioButton
        Accessible.checked: opt.selected

        contentItem: RowLayout {
            spacing: 10
            Rectangle {
                Layout.leftMargin: 2
                implicitWidth: 18
                implicitHeight: 18
                radius: width / 2
                color: "transparent"
                border.width: opt.selected ? 5 : 1
                border.color: opt.selected ? Theme.accent : Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                text: opt.text
                color: Theme.textPrimary
                font.pixelSize: 14
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: opt.hovered ? Theme.menuHover : "transparent"
            radius: 8
        }
    }

    // A theme drawn in its own colors, which is the whole of the preview: the
    // name over the surface it would give the app, beside its accent and its
    // outgoing bubble.
    component ThemeChip: Rectangle {
        id: chip
        // The palette this chip picks; empty is the system's own, drawn in
        // whichever palette that resolves to.
        property string themeName: ""
        readonly property bool system: chip.themeName === ""
        readonly property var pal: Theme.palettes[chip.system ? Theme.systemName : chip.themeName]
        readonly property bool current: chip.system ? Theme.followSystem
                                                    : !Theme.followSystem && Theme.name === chip.themeName

        objectName: "theme_" + (chip.system ? "system" : chip.themeName)
        implicitWidth: chipRow.implicitWidth + 20
        implicitHeight: 36
        radius: 10
        color: chip.pal.surface
        border.width: chip.current ? 2 : 1
        border.color: chip.current ? Theme.accent : Theme.hairline

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                implicitWidth: 12; implicitHeight: 12
                radius: width / 2
                color: chip.pal.accent
            }
            Rectangle {
                implicitWidth: 12; implicitHeight: 12
                radius: width / 2
                color: chip.pal.bubbleOut
                border.width: 1
                border.color: chip.pal.hairline
            }
            Text {
                text: chip.system ? qsTr("system") : chip.themeName
                color: chip.pal.textPrimary
                font.pixelSize: 13
                font.bold: chip.current
            }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler {
            onTapped: chip.system ? Theme.followSystem = true : Theme.choose(chip.themeName)
        }
    }

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
            Text {
                Layout.fillWidth: true
                text: qsTr("Preferences")
                color: Theme.textPrimary
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            Item { Layout.preferredHeight: 4 }

            Card {
                SectionTitle { text: qsTr("Images") }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("What may be fetched without asking. Shared by every account.")
                    wrapMode: Text.WordWrap
                }

                Caption { Layout.topMargin: 4; text: qsTr("Load images") }
                Repeater {
                    model: [
                        { label: qsTr("From everyone"), value: "everyone" },
                        { label: qsTr("From contacts only"), value: "contacts" },
                        { label: qsTr("Never"), value: "never" }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "autofetch_" + modelData.value
                        text: modelData.label
                        selected: App.settings.attachmentAutofetch === modelData.value
                        onClicked: App.settings.setAttachmentAutofetch(modelData.value)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }

                Caption { text: qsTr("Max image size") }
                Repeater {
                    model: [
                        { label: qsTr("1 MB"), value: 1048576 },
                        { label: qsTr("5 MB"), value: 5242880 },
                        { label: qsTr("25 MB"), value: 26214400 },
                        { label: qsTr("Unlimited"), value: 0 }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "autofetchMax_" + modelData.value
                        text: modelData.label
                        selected: App.settings.attachmentAutofetchMax === modelData.value
                        onClicked: App.settings.setAttachmentAutofetchMax(modelData.value)
                    }
                }

                Caption {
                    Layout.fillWidth: true
                    text: qsTr("A picture over the limit waits for a tap instead.")
                    wrapMode: Text.WordWrap
                }
            }

            Card {
                SectionTitle { text: qsTr("Appearance") }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    ThemeChip {}

                    Repeater {
                        model: Object.keys(Theme.palettes)
                        delegate: ThemeChip {
                            required property string modelData
                            themeName: modelData
                        }
                    }
                }

                // Nothing stores the pick yet.
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("The app starts on the system's light or dark setting each time.")
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    implicitHeight: 1
                    color: Theme.hairline
                }

                CheckBox {
                    id: avatarBox
                    objectName: "chatAvatarsBox"
                    Layout.fillWidth: true
                    padding: 0
                    text: qsTr("Show avatars in chats")
                    checked: App.settings.chatAvatars
                    onToggled: App.settings.setChatAvatars(checked)
                    contentItem: Text {
                        text: avatarBox.text
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        leftPadding: avatarBox.indicator.width + 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("One picture per run of messages from the same person.")
                    wrapMode: Text.WordWrap
                }
            }

            // Nothing here takes effect now: both actions only request a
            // migration, which tacky runs at the next startup before any
            // account connects. StorageGate collects the passphrase there.
            Card {
                id: storageCard
                objectName: "storageCard"

                readonly property string status: App.storage.status
                readonly property bool pending: storageCard.status.startsWith("pending-")

                SectionTitle { text: qsTr("Local storage") }

                Text {
                    objectName: "storageStatus"
                    Layout.fillWidth: true
                    text: {
                        // Not yet answered, which is not an unencrypted store.
                        if (storageCard.status === "")
                            return qsTr("Checking…")
                        if (storageCard.status === "plaintext"
                                || storageCard.status === "pending-encrypt")
                            return qsTr("Messages and accounts on this device are not encrypted.")
                        return qsTr("Messages and accounts on this device are encrypted.")
                    }
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                }

                Caption {
                    objectName: "storageDetail"
                    Layout.fillWidth: true
                    text: {
                        switch (storageCard.status) {
                        case "plaintext":
                            return qsTr("Encrypting sets a passphrase you will be asked for every time Quack starts. There is no way to recover the data if you forget it. Takes effect the next time Quack starts.")
                        case "unlocked":
                            return qsTr("Removing encryption writes everything back out as plaintext. Takes effect the next time Quack starts.")
                        case "pending-encrypt":
                            return qsTr("Encryption is set to be enabled the next time Quack starts.")
                        case "pending-decrypt":
                            return qsTr("Removing encryption is set to happen the next time Quack starts.")
                        default:
                            return ""
                        }
                    }
                    wrapMode: Text.WordWrap
                }

                Text {
                    objectName: "storageError"
                    Layout.fillWidth: true
                    visible: App.storage.error !== ""
                    text: App.storage.error
                    color: Theme.negative
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }

                Button {
                    id: storageAction
                    objectName: "storageActionButton"
                    // Nothing to offer until the status is known, and a locked
                    // store never reaches this page - the gate is in front of
                    // it.
                    visible: storageCard.status !== ""
                             && storageCard.status !== "locked"
                    enabled: !App.storage.busy
                    flat: true
                    padding: 0
                    text: {
                        switch (storageCard.status) {
                        case "plaintext":
                            return qsTr("Encrypt local storage…")
                        case "unlocked":
                            return qsTr("Remove encryption…")
                        case "pending-encrypt":
                            return qsTr("Cancel pending encryption")
                        case "pending-decrypt":
                            return qsTr("Cancel pending removal")
                        default:
                            return ""
                        }
                    }
                    onClicked: {
                        if (storageCard.pending)
                            App.storage.cancelPending()
                        else if (storageCard.status === "plaintext")
                            storageConfirm.ask(
                                "encrypt",
                                qsTr("You will be asked to set a passphrase the next time Quack starts. There is no way to recover your data if you forget it. Continue?"))
                        else
                            storageConfirm.ask(
                                "decrypt",
                                qsTr("Local storage will be written back out as plaintext the next time Quack starts. Continue?"))
                    }
                    contentItem: Text {
                        text: storageAction.text
                        color: Theme.accent
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Card {
                id: callsCard

                function backendLabel(name) {
                    if (name === "rtc")
                        return qsTr("Built in")
                    if (name === "webrtc")
                        return qsTr("libwebrtc")
                    return name
                }

                SectionTitle { text: qsTr("Calls") }

                Caption { text: qsTr("Media backend") }
                Repeater {
                    model: [
                        { label: qsTr("Automatic"), value: "" },
                        { label: qsTr("Built in"), value: "rtc" },
                        { label: qsTr("libwebrtc"), value: "webrtc" }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "mediaBackend_" + (modelData.value || "auto")
                        text: modelData.label
                        selected: App.settings.mediaBackend === modelData.value
                        onClicked: App.settings.setMediaBackend(modelData.value)
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Automatic uses libwebrtc where this build carries it. Takes effect at the next start.")
                    wrapMode: Text.WordWrap
                }
                // The choice above is what the next start will try; this is
                // what this one ended up on.
                Caption {
                    objectName: "mediaBackendRunning"
                    Layout.fillWidth: true
                    visible: App.settings.activeMediaBackend !== ""
                    text: qsTr("Running now: %1.")
                        .arg(callsCard.backendLabel(App.settings.activeMediaBackend))
                    wrapMode: Text.WordWrap
                }
            }

            Card {
                SectionTitle { text: qsTr("Diagnostics") }

                CheckBox {
                    id: logBox
                    objectName: "logToFileBox"
                    Layout.fillWidth: true
                    padding: 0
                    // The style centres its indicator when the control has no
                    // text of its own, so set it even though contentItem draws it.
                    text: qsTr("Write a log file")
                    checked: App.settings.logToFile
                    onToggled: App.settings.setLogToFile(checked)
                    contentItem: Text {
                        text: logBox.text
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        leftPadding: logBox.indicator.width + 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Turn this on, do the thing that goes wrong, then send the log with your report.")
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }

                Caption { text: qsTr("Log level") }
                Repeater {
                    // tacky's levels, less `fatal`: a log holding only what
                    // killed the process has nothing to say about how it got
                    // there. The Tk client offers the same six.
                    model: [
                        { label: qsTr("Verbose"), value: "verbose" },
                        { label: qsTr("Debug"), value: "debug" },
                        { label: qsTr("Info"), value: "info" },
                        { label: qsTr("Warning"), value: "warning" },
                        { label: qsTr("Error"), value: "error" },
                        { label: qsTr("Off"), value: "none" }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "logLevel_" + modelData.value
                        text: modelData.label
                        selected: App.settings.logLevel === modelData.value
                        onClicked: App.settings.setLogLevel(modelData.value)
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Debug and Verbose add every stanza the connection carries, message text included.")
                    wrapMode: Text.WordWrap
                }

                CheckBox {
                    id: nativeBox
                    objectName: "logNativeBox"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    padding: 0
                    text: qsTr("Log WebRTC internals")
                    checked: App.settings.logNative
                    onToggled: App.settings.setLogNative(checked)
                    contentItem: Text {
                        text: nativeBox.text
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        leftPadding: nativeBox.indicator.width + 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("For calls that will not connect. Very noisy, and it ignores the level above.")
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }
                Caption {
                    Layout.fillWidth: true
                    // Not a footnote: it is the whole of the informed part of
                    // consenting to hand the file over.
                    color: Theme.textPrimary
                    text: qsTr("The log holds who you talk to, and can hold what you said. Only send it to someone you trust.")
                    wrapMode: Text.WordWrap
                }

                // Only once there is a file, which is also the moment the path
                // below stops being empty.
                Button {
                    id: exportBtn
                    objectName: "exportLogButton"
                    visible: App.logPath !== ""
                    flat: true
                    padding: 0
                    // Android keeps the file where no file manager can reach
                    // it, so there the only way out is the share chooser.
                    readonly property bool sharing: Qt.platform.os === "android"
                    text: exportBtn.sharing ? qsTr("Send the log…")
                                            : qsTr("Open the folder")
                    onClicked: {
                        if (exportBtn.sharing)
                            App.shareLog()
                        else
                            Qt.openUrlExternally(App.logFolder())
                    }
                    contentItem: Text {
                        text: exportBtn.text
                        color: Theme.accent
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    visible: App.logPath !== "" && !exportBtn.sharing
                    text: App.logPath
                    wrapMode: Text.WrapAnywhere
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }

    // Both storage actions are worth asking about twice: one costs the data if
    // the passphrase is lost, the other gives up the protection. `subject`
    // carries which one, the way the other pages' shared confirmations do.
    ConfirmDialog {
        id: storageConfirm
        objectName: "storageConfirm"
        // Read off the page, not off the dialog. SheetDialog's own
        // `parent: Overlay.overlay` asks the dialog which window it is in, and
        // a Popup only knows that from the parent being assigned - so with
        // Preferences in a window of its own the answer came back as the
        // shell's overlay and the question appeared over the wrong window. The
        // page is an item and always knows.
        parent: page.Overlay.overlay
        title: subject === "encrypt" ? qsTr("Encrypt local storage")
                                     : qsTr("Remove encryption")

        function ask(direction, text) {
            subject = direction
            message = text
            open()
        }

        onAccepted: {
            if (storageConfirm.subject === "encrypt")
                App.storage.requestEncrypt()
            else
                App.storage.requestDecrypt()
        }
    }
}
