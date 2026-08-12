pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One account's details: the stored credential, the published name, this
// device's OMEMO key, and the trust state of every other device the account
// has. Hosted by AccountSettingsWindow on desktop and as a full-screen sheet
// on mobile, so it carries its own header and footer.
//
// The credential and the name are edited and then saved. The OMEMO controls
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
    function bindAccount() {
        page.settings = page.account !== "" ? App.accountSettingsFor(page.account) : null
        page.devices = page.settings ? page.settings.devices : null
        page.editingNick = false
        passwordField.text = Qt.binding(() => page.settings ? page.settings.password : "")
        nickField.text = Qt.binding(() => page.settings ? page.settings.nick : "")
    }
    onAccountChanged: page.bindAccount()
    Component.onCompleted: page.bindAccount()

    readonly property bool dirty: page.settings !== null
                                  && (passwordField.text !== page.settings.password
                                      || nickField.text !== page.settings.nick)

    background: Rectangle { color: Theme.background }

    function copyFingerprint(spaced) {
        Clipboard.setText(spaced)
        copiedNotice.show()
    }

    Connections {
        target: page.settings
        function onSaved() { page.done() }
    }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
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
                text: "Account details"
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

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            Item { Layout.preferredHeight: 4 }

            // Who this account is, and what it signs in with.
            Card {
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    Avatar {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 72
                        Layout.alignment: Qt.AlignTop
                        radius: 10
                        account: page.account
                        jid: page.account
                        label: page.account
                        initialsPixelSize: 26
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Caption { text: "XMPP address" }
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
                            Caption { text: "Password" }
                            TextField {
                                id: passwordField
                                objectName: "passwordField"
                                Layout.fillWidth: true
                                // Armed by bindAccount, which owns both fields.
                                echoMode: TextInput.Password
                                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
                                onAccepted: if (page.dirty) page.save()
                            }
                        }

                        // Saving the credential does not re-authenticate.
                        Caption {
                            Layout.fillWidth: true
                            visible: page.settings !== null
                                     && passwordField.text !== page.settings.password
                            text: "Takes effect the next time this account connects."
                            wrapMode: Text.WordWrap
                        }
                    }
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
                            text: nickField.text !== "" ? nickField.text : "Not set"
                            color: nickField.text !== "" ? Theme.textPrimary : Theme.textDim
                            font.pixelSize: 16
                            elide: Text.ElideRight
                        }
                        TextField {
                            id: nickField
                            objectName: "nickField"
                            Layout.fillWidth: true
                            visible: page.editingNick
                            placeholderText: "Your name"
                            onAccepted: {
                                page.editingNick = false
                                if (page.dirty)
                                    page.save()
                            }
                        }
                        Caption { text: "Your name" }
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
                    note: "this device"
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }

                // The OMEMO store is built when the account first connects, so
                // there is no key to show before that.
                Caption {
                    Layout.fillWidth: true
                    visible: page.devices !== null && page.devices.ownFingerprint === ""
                    text: "This device gets its key once the account has connected."
                    wrapMode: Text.WordWrap
                }
            }

            // Every other device this account has.
            Card {
                SectionTitle { text: "Other devices" }

                CheckBox {
                    id: blindTrustBox
                    objectName: "blindTrustBox"
                    Layout.fillWidth: true
                    padding: 0
                    // The style centres its indicator when the control has no
                    // text of its own, so set it even though contentItem draws it.
                    text: "Trust new devices automatically"
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
                    text: "Applies to this account. New keys are trusted until you decide otherwise."
                    wrapMode: Text.WordWrap
                }

                TrustList {
                    devices: page.devices
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }

                Caption {
                    Layout.fillWidth: true
                    visible: page.devices !== null && page.devices.count === 0
                    text: "No other devices yet."
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
                text: "Cancel"
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
                text: page.settings && page.settings.saving ? "Saving" : "Save"
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
            page.settings.save(passwordField.text, nickField.text)
    }

    // A copy is silent otherwise.
    Rectangle {
        id: copiedNotice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: noticeText.implicitWidth + 28
        height: 34
        radius: 17
        color: Theme.textPrimary
        opacity: 0
        visible: opacity > 0

        function show() {
            copiedNotice.opacity = 0.92
            hideTimer.restart()
        }

        Behavior on opacity { NumberAnimation { duration: 180 } }
        Timer {
            id: hideTimer
            interval: 1400
            onTriggered: copiedNotice.opacity = 0
        }

        Text {
            id: noticeText
            anchors.centerIn: parent
            text: "Fingerprint copied"
            color: Theme.surface
            font.pixelSize: 12
        }
    }
}
