pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One contact's OMEMO devices and what we make of each of them. Reached from
// the padlock in their chat, and hosted the same two ways the account page is:
// a window on desktop, a full-screen sheet on mobile.
//
// Trust is written the moment it is picked, so there is nothing to save and
// nothing to cancel. Blind trust is deliberately absent - it is an account-wide
// setting and belongs with the account, not with one conversation.
Page {
    id: page
    objectName: "omemoKeysPage"

    property string account: ""
    property string jid: ""
    property string name: ""
    property bool showClose: true
    signal done

    background: Rectangle { color: Theme.background }

    OmemoDevicesModel {
        id: devices
        backend: App.backend
        account: page.account
        jid: page.jid
    }

    // Our own key, for the half of the comparison they are reading out. It
    // takes a second model because only a model whose subject is the account
    // itself asks for the fingerprint of this device.
    OmemoDevicesModel {
        id: ownKey
        backend: App.backend
        account: page.account
        jid: page.account
    }

    // QML has no clipboard of its own; a TextEdit's copy() is the way to one.
    function copyFingerprint(spaced) {
        clipboard.text = spaced
        clipboard.selectAll()
        clipboard.copy()
        clipboard.deselect()
        copiedNotice.show()
    }

    TextEdit {
        id: clipboard
        width: 0
        height: 0
        opacity: 0
        activeFocusOnPress: false
    }

    component Caption: Text {
        color: Theme.textDim
        font.pixelSize: 11
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

    header: Rectangle {
        height: 60
        color: Theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: page.showClose ? 4 : 16
            anchors.rightMargin: 8
            spacing: 4
            IconButton {
                text: "←"
                visible: page.showClose
                glyphColor: Theme.textDim
                onClicked: page.done()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: "Encryption keys"
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: page.name !== "" && page.name !== page.jid
                          ? page.name + " — " + page.jid : page.jid
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: Theme.hairline
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 32
        clip: true

        ColumnLayout {
            id: column
            width: parent.width - 32
            x: 16
            y: 16
            spacing: 14

            Card {
                Text {
                    text: "Their devices"
                    color: Theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }
                Caption {
                    Layout.fillWidth: true
                    text: "Compare a key with them over another channel before trusting it."
                    wrapMode: Text.WordWrap
                }

                // Nothing arrives for a contact until their device list does,
                // and none of it arrives before the account has connected.
                Caption {
                    Layout.fillWidth: true
                    objectName: "noKeysNotice"
                    visible: devices.count === 0
                    text: "No keys for this contact yet. They appear once their devices announce themselves."
                    wrapMode: Text.WordWrap
                }

                // Only worth offering once there is more than one to set.
                ColumnLayout {
                    objectName: "setAllRow"
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    visible: devices.settableCount >= 2
                    spacing: 4
                    Caption { text: "Set all"; font.bold: true }
                    TrustPicker {
                        objectName: "setAllPicker"
                        trust: devices.commonTrust
                        onPicked: (newTrust) => devices.setAllTrust(newTrust)
                    }
                }

                Repeater {
                    objectName: "deviceList"
                    model: devices

                    delegate: ColumnLayout {
                        id: deviceRow
                        required property int device
                        required property string trust
                        required property bool active
                        required property string fingerprint
                        required property bool settable

                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 6

                        Fingerprint {
                            Layout.fillWidth: true
                            hex: deviceRow.fingerprint
                            note: deviceRow.active ? "" : "(inactive)"
                            onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                        }

                        // The backend pins a rotated key, so there is nothing
                        // to pick on this row.
                        Text {
                            Layout.fillWidth: true
                            visible: !deviceRow.settable
                            text: "Compromised - key changed"
                            color: Theme.negative
                            font.pixelSize: 12
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        TrustPicker {
                            objectName: "devicePicker"
                            visible: deviceRow.settable
                            trust: deviceRow.trust
                            onPicked: (newTrust) => devices.setTrust(deviceRow.device, newTrust)
                        }
                    }
                }
            }

            // Verifying runs both ways, so the key they will be reading back
            // to you belongs on the same screen.
            Card {
                Text {
                    text: "This device"
                    color: Theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }
                Fingerprint {
                    Layout.fillWidth: true
                    objectName: "ownFingerprint"
                    visible: ownKey.ownFingerprint !== ""
                    hex: ownKey.ownFingerprint
                    note: "yours"
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }
                Caption {
                    Layout.fillWidth: true
                    visible: ownKey.ownFingerprint === ""
                    text: "This device gets its key once the account has connected."
                    wrapMode: Text.WordWrap
                }
            }
        }
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
