pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Quack

// One account's details: the stored login password (and a way to change the
// one on the server), the proxy it connects through, the published name, this device's OMEMO key, and the
// trust state of every other device the account has. Hosted by
// AccountSettingsWindow on desktop and as a full-screen sheet on mobile, so it
// carries its own header and footer.
//
// The credential, the proxy and the name are edited and then saved. The OMEMO controls
// are not: a trust change is written the moment it is picked, so Cancel has
// nothing to undo there.
Page {
    id: page
    objectName: "accountSettingsPage"

    property string account: ""
    property bool showClose: true
    // The name is a label until the pencil turns it into a field.
    property bool editingNick: false
    signal done

    // Resolved on demand rather than bound: a binding that calls into App
    // re-runs when the object it returned is destroyed, and at shutdown that
    // means calling a singleton that is already on its way out.
    property var settings: null
    property var devices: null

    // Also re-arms the two fields: typing into one replaces its binding, and
    // the mobile sheet is reused for whichever account is opened next.
    // Back to showing the stored password, after an edit broke the binding.
    function bindPassword() {
        passwordField.text = Qt.binding(() => page.settings ? page.settings.password : "")
    }
    function bindAccount() {
        page.settings = page.account !== "" ? App.accountSettingsFor(page.account) : null
        page.devices = page.settings ? page.settings.devices : null
        page.editingNick = false
        page.bindPassword()
        nickField.text = Qt.binding(() => page.settings ? page.settings.nick : "")
        proxyField.text = Qt.binding(() => page.settings ? page.settings.proxy : "")
    }
    onAccountChanged: page.bindAccount()
    Component.onCompleted: page.bindAccount()

    // Why the account's last connection attempt failed, until one gets through.
    // connRev is read only to give the call a dependency.
    readonly property string connError: {
        App.accounts.connRev
        return page.account !== "" ? App.accounts.connErrorFor(page.account) : ""
    }
    // tacky retries a transport failure for good, but stops on rejected
    // credentials (auth-error) or a stream error retrying won't cure
    // (disconnected), until the account is enabled again or a connection
    // field changes.
    readonly property bool connGaveUp: {
        App.accounts.connRev
        const state = page.account !== "" ? App.accounts.connStateFor(page.account) : ""
        return state === "auth-error" || state === "disconnected"
    }

    readonly property bool dirty: page.settings !== null
                                  && (passwordField.text !== page.settings.password
                                      || nickField.text !== page.settings.nick
                                      || proxyField.text.trim() !== page.settings.proxy)

    // Whether this account has an avatar published, which is what decides if
    // there is anything to remove. The hash is the truth here rather than the
    // loaded picture, so the control does not blink out while one is fetching;
    // `rev` is read only to give the binding something to depend on, as in
    // Avatar.qml.
    readonly property bool hasAvatar: {
        const rev = App.avatars.rev
        return rev >= 0 && page.account !== ""
               && App.avatars.hashFor(page.account, page.account) !== ""
    }

    // Nothing can be done to the picture while one is already on its way out.
    readonly property bool avatarEditable: page.settings !== null
                                           && !page.settings.avatarBusy

    background: Rectangle { color: Theme.background }

    function copyFingerprint(spaced) {
        Clipboard.setText(spaced)
        copiedNotice.text = qsTr("Fingerprint copied")
        copiedNotice.show()
    }

    Connections {
        target: page.settings
        function onSaved() { page.done() }
        // tacky stored the new password too, so the field shows that one now.
        function onServerPasswordChanged() { page.bindPassword() }
    }

    ChangePasswordDialog {
        id: changePasswordDialog
        settings: page.settings
    }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
    }

    component Caption: Text {
        color: Theme.textDim
        font.pixelSize: 11
    }

    // The picture is published on its own, so this is not wired to Save.
    FileDialog {
        id: avatarDialog
        objectName: "avatarDialog"
        title: qsTr("Choose a picture")
        // Android's picker filters by MIME type, which FileDialog does not
        // expose, so it lists everything there; the decode is what turns a
        // non-image away.
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.webp)"),
                      qsTr("All files (*)")]
        onAccepted: if (page.settings) page.settings.setAvatar(avatarDialog.selectedFile)
    }

    // An action that reads as a link, as "Change password on server…" does.
    component LinkButton: Button {
        id: link
        flat: true
        padding: 0
        contentItem: Text {
            text: link.text
            color: Theme.accent
            font.pixelSize: 14
        }
        background: null
    }

    component Card: Rectangle {
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 28
        color: Theme.surface
        radius: 12
        border.width: 1
        border.color: Theme.hairline

        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10
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
                text: qsTr("Account details")
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

            // The picture leads the page rather than sharing a card with the
            // fields, and both things that can be done to it ride on a chip
            // over its lower edge: no second page to open, and nothing to aim
            // at on a phone. Tapping the picture sets one too, which is the
            // large target for the common action.
            Item {
                id: avatarEditor
                objectName: "avatarEditor"
                readonly property int edge: 112

                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                Layout.preferredWidth: avatarEditor.edge
                // The chip hangs half of itself past the picture's bottom.
                Layout.preferredHeight: avatarEditor.edge + avatarActions.height / 2

                Avatar {
                    id: accountAvatar
                    objectName: "accountAvatar"
                    width: avatarEditor.edge
                    height: avatarEditor.edge
                    account: page.account
                    jid: page.account
                    label: page.account
                    initialsPixelSize: 40
                    opacity: page.settings && page.settings.avatarBusy ? 0.5 : 1
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }

                // Pressed feedback, in the picture's own shape.
                Rectangle {
                    width: accountAvatar.width
                    height: accountAvatar.height
                    radius: accountAvatar.radius
                    antialiasing: true
                    color: Theme.textPrimary
                    opacity: avatarTap.pressed ? 0.18 : 0
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                }

                HoverHandler {
                    enabled: avatarTap.enabled
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    id: avatarTap
                    objectName: "avatarTap"
                    enabled: page.avatarEditable
                    onTapped: avatarDialog.open()
                }

                Rectangle {
                    id: avatarActions
                    objectName: "avatarActions"
                    anchors.horizontalCenter: accountAvatar.horizontalCenter
                    anchors.verticalCenter: accountAvatar.bottom
                    width: actionRow.implicitWidth + 4
                    height: 40
                    radius: height / 2
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.hairline
                    visible: setAvatar.visible || removeAvatar.visible

                    Row {
                        id: actionRow
                        anchors.centerIn: parent

                        IconButton {
                            id: setAvatar
                            objectName: "avatarSetButton"
                            text: "📷"
                            Accessible.name: qsTr("Choose a picture")
                            visible: page.avatarEditable
                            onClicked: avatarDialog.open()
                        }

                        // Offered only once there is something to remove.
                        IconButton {
                            id: removeAvatar
                            objectName: "avatarRemoveButton"
                            text: "🗑️"
                            Accessible.name: qsTr("Remove the picture")
                            visible: page.avatarEditable && page.hasAvatar
                            onClicked: if (page.settings) page.settings.clearAvatar()
                        }
                    }
                }
            }

            // Upload progress while it is out, and why it failed when it did.
            // A success says nothing: the new picture is right above this.
            Caption {
                objectName: "avatarStatus"
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                horizontalAlignment: Text.AlignHCenter
                visible: text !== ""
                text: page.settings ? page.settings.avatarStatus : ""
                color: page.settings && page.settings.avatarError ? Theme.negative
                                                                  : Theme.textDim
                wrapMode: Text.WordWrap
            }

            // What is wrong with the account, first thing on the page and only
            // while something is: why it can't connect, whatever the cause.
            // The rail and the chat list say it in a line; this is where it
            // is read in full, selected or copied, and acted on.
            Card {
                objectName: "problemCard"
                visible: page.connError !== ""
                border.color: Theme.negative

                SectionTitle {
                    text: page.connGaveUp ? qsTr("Stopped connecting")
                                          : qsTr("Can't connect")
                }

                // tacky's own sentence, as it stands.
                TextEdit {
                    objectName: "problemText"
                    Layout.fillWidth: true
                    text: page.connError
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.negative
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    font.pixelSize: 14
                }

                Caption {
                    Layout.fillWidth: true
                    text: page.connGaveUp
                          ? qsTr("Quack won't retry this on its own. Fix the cause, or try again.")
                          : qsTr("Retrying on its own, less often as it keeps failing.")
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16

                    LinkButton {
                        objectName: "copyProblem"
                        text: qsTr("Copy")
                        onClicked: {
                            Clipboard.setText(page.connError)
                            copiedNotice.text = qsTr("Copied")
                            copiedNotice.show()
                        }
                    }
                    // `enable` on an enabled account that is offline is
                    // tacky's reconnect, and skips what is left of the wait.
                    LinkButton {
                        objectName: "retryNow"
                        text: qsTr("Try again now")
                        onClicked: App.accounts.enable(page.account)
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // Who this account is, and what it signs in with.
            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Caption { text: qsTr("XMPP address") }
                    Text {
                        Layout.fillWidth: true
                        text: page.account
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Caption { text: qsTr("Login password") }
                    TextField {
                        id: passwordField
                        objectName: "passwordField"
                        Layout.fillWidth: true
                        // Armed by bindAccount, which owns both fields.
                        echoMode: revealBox.checked ? TextInput.Normal : TextInput.Password
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
                        onAccepted: if (page.dirty) page.save()
                    }
                }

                RevealBox {
                    id: revealBox
                    objectName: "passwordReveal"
                    Layout.fillWidth: true
                }

                // Only the copy on this device: tacky retries a failed sign-in
                // with it, and leaves a session that is already up alone.
                Caption {
                    Layout.fillWidth: true
                    visible: page.settings !== null
                             && passwordField.text !== page.settings.password
                    text: qsTr("Changes only the password Quack signs in with, not the one on the server.")
                    wrapMode: Text.WordWrap
                }

                Button {
                    id: changeOnServer
                    objectName: "changePasswordOnServer"
                    text: qsTr("Change password on server…")
                    flat: true
                    padding: 0
                    enabled: page.settings !== null
                    onClicked: changePasswordDialog.open()
                    contentItem: Text {
                        text: changeOnServer.text
                        color: changeOnServer.enabled ? Theme.accent : Theme.textDim
                        font.pixelSize: 14
                    }
                    background: null
                }
            }

            // How the account reaches its server.
            Card {
                SectionTitle { text: qsTr("Connection") }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Caption { text: qsTr("SOCKS5 proxy") }
                    TextField {
                        id: proxyField
                        objectName: "proxyField"
                        Layout.fillWidth: true
                        // Armed by bindAccount. It can carry a login, so no
                        // suggestions.
                        placeholderText: qsTr("socks5://host:port")
                        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
                                          | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
                        onAccepted: if (page.dirty) page.save()
                    }
                }

                // tacky's reason for refusing what was saved, in full.
                Caption {
                    objectName: "proxyError"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: page.settings ? page.settings.proxyError : ""
                    color: Theme.negative
                    wrapMode: Text.WordWrap
                }

                Caption {
                    Layout.fillWidth: true
                    text: proxyField.text.trim() !== ""
                          ? qsTr("Everything this account sends goes through the proxy, files included. Calls are off while it is set.")
                          : qsTr("Leave empty to connect directly. A login goes in as socks5://user:password@host:port.")
                    wrapMode: Text.WordWrap
                }

                // tacky starts the account over on the new route.
                Caption {
                    Layout.fillWidth: true
                    visible: page.settings !== null
                             && proxyField.text.trim() !== page.settings.proxy
                    text: qsTr("Saving reconnects the account, ending any call or transfer it has going.")
                    wrapMode: Text.WordWrap
                }
            }

            // The name others see, and this device's own key.
            Card {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            visible: !page.editingNick
                            text: nickField.text !== "" ? nickField.text : qsTr("Not set")
                            color: nickField.text !== "" ? Theme.textPrimary : Theme.textDim
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        TextField {
                            id: nickField
                            objectName: "nickField"
                            Layout.fillWidth: true
                            visible: page.editingNick
                            placeholderText: qsTr("Your name")
                            onAccepted: {
                                page.editingNick = false
                                if (page.dirty)
                                    page.save()
                            }
                        }
                        Caption { text: qsTr("Your name") }
                    }

                    IconButton {
                        iconPath: Icons.edit
                        Accessible.name: qsTr("Edit your name")
                        glyphColor: page.editingNick ? Theme.accent : Theme.textDim
                        onClicked: {
                            page.editingNick = !page.editingNick
                            if (page.editingNick)
                                nickField.forceActiveFocus()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }

                Fingerprint {
                    Layout.fillWidth: true
                    visible: page.devices !== null && page.devices.ownFingerprint !== ""
                    hex: page.devices ? page.devices.ownFingerprint : ""
                    note: qsTr("this device")
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }

                // The OMEMO store is built when the account first connects, so
                // there is no key to show before that.
                Caption {
                    Layout.fillWidth: true
                    visible: page.devices !== null && page.devices.ownFingerprint === ""
                    text: qsTr("This device gets its key once the account has connected.")
                    wrapMode: Text.WordWrap
                }
            }

            // Every other device this account has.
            Card {
                SectionTitle { text: qsTr("Other devices") }

                CheckBox {
                    id: blindTrustBox
                    objectName: "blindTrustBox"
                    Layout.fillWidth: true
                    padding: 0
                    // The style centres its indicator when the control has no
                    // text of its own, so set it even though contentItem draws it.
                    text: qsTr("Trust new devices automatically")
                    checked: page.devices ? page.devices.blindTrust : false
                    onToggled: if (page.devices) page.devices.blindTrust = checked
                    contentItem: Text {
                        text: blindTrustBox.text
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        leftPadding: blindTrustBox.indicator.width + 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Applies to this account. New keys are trusted until you decide otherwise.")
                    wrapMode: Text.WordWrap
                }

                TrustList {
                    devices: page.devices
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }

                Caption {
                    Layout.fillWidth: true
                    visible: page.devices !== null && page.devices.count === 0
                    text: qsTr("No other devices yet.")
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }

    footer: Rectangle {
        height: 60
        color: Theme.surface

        Rectangle {
            width: parent.width; height: 1
            color: Theme.hairline
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: page.settings ? page.settings.status : ""
                color: page.settings && page.settings.statusError ? Theme.negative
                                                                  : Theme.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Button {
                id: cancelBtn
                text: qsTr("Cancel")
                flat: true
                onClicked: page.done()
                contentItem: Text {
                    text: cancelBtn.text
                    color: Theme.textDim
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: saveBtn
                text: page.settings && page.settings.saving ? qsTr("Saving") : qsTr("Save")
                enabled: page.dirty && !(page.settings && page.settings.saving)
                onClicked: page.save()
                contentItem: Text {
                    text: saveBtn.text
                    color: saveBtn.enabled ? Theme.textOnAccent : Theme.textDim
                    font.pixelSize: 14
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitWidth: 92
                    implicitHeight: 36
                    radius: 18
                    color: saveBtn.enabled ? (saveBtn.pressed ? Theme.accentDeep : Theme.accent)
                                           : Theme.field
                }
            }
        }
    }

    function save() {
        if (page.settings)
            page.settings.save(passwordField.text, nickField.text, proxyField.text.trim())
    }

    CopiedNotice {
        id: copiedNotice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }
}
