pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One contact: who the roster says they are, and what we make of each of their
// OMEMO devices. The mirror of AccountSettingsPage, for the other side of a
// conversation. Reached from the chat's header and from the padlock beside the
// message box, and hosted the same two ways the account page is: a window on
// desktop, a full-screen sheet on mobile.
//
// Trust is written the moment it is picked, and nothing else here is editable,
// so there is nothing to save and nothing to cancel. Blind trust is
// deliberately absent - it is an account-wide setting and belongs with the
// account, not with one conversation.
Page {
    id: page
    objectName: "contactDetailsPage"

    property string account: ""
    property string jid: ""
    property string name: ""
    property bool showClose: true
    signal done

    background: Rectangle { color: Theme.background }

    // The chat list's record of this contact, which is the roster's. Empty for a
    // JID it has never carried.
    property var entry: ({})

    // Resolved on demand rather than bound, as on the account page: a binding
    // that calls into App re-runs when the object it returned is destroyed, and
    // at shutdown that means calling a singleton already on its way out.
    property var chatList: null

    function bindContact() {
        page.chatList = page.jid !== "" ? App.chatListFor(page.account) : null
        page.readEntry()
    }
    onAccountChanged: page.bindContact()
    onJidChanged: page.bindContact()
    Component.onCompleted: page.bindContact()

    // entryFor is a lookup over the list's rows rather than a property, so
    // nothing would tell a binding of it that the roster had moved. These are
    // every way a row can: a rename or an answered subscription arrives as a
    // change, being added or removed as a row, a reload as a reset.
    function readEntry() {
        page.entry = page.chatList ? page.chatList.entryFor(page.jid) : ({})
    }
    Connections {
        target: page.chatList
        function onDataChanged() { page.readEntry() }
        function onModelReset() { page.readEntry() }
        function onRowsInserted() { page.readEntry() }
        function onRowsRemoved() { page.readEntry() }
    }

    // The roster's name for them, which need not be the one the chat was opened
    // under: a rename lands here first. Falls back to what the caller passed,
    // then to the JID, so the page always has something to call them.
    readonly property string displayName: {
        const rosterName = page.entry.name ?? ""
        return rosterName !== "" ? rosterName : page.name
    }

    // A chat can outlive a roster entry, or never have had one: `source` is
    // "free" for a JID we only have history with.
    readonly property bool rostered: page.entry.source === "roster"

    // What the two of you have agreed to share, as the roster has it. tacky
    // hands the roster item over as it stored it, so `ask` rides along in the
    // entry even though the chat list has no role for it.
    readonly property string sharingNote: {
        const sub = page.entry.subscription ?? ""
        if (sub === "both")
            return qsTr("You can each see the other's status.")
        if (sub === "to")
            return qsTr("You see their status. They cannot see yours.")
        if (sub === "from")
            return qsTr("They see your status. You cannot see theirs.")
        if (page.entry.ask === "subscribe")
            return qsTr("Waiting for them to approve your request.")
        return qsTr("Neither of you can see the other's status.")
    }

    OmemoDevicesModel {
        id: theirKeys
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

    function copyFingerprint(spaced) {
        Clipboard.setText(spaced)
        copiedNotice.show()
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
                text: qsTr("Contact details")
                color: Theme.textPrimary
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }
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

            // The picture leads the page as on the account page, without that
            // page's controls: what they publish is not ours to set.
            Avatar {
                objectName: "contactAvatar"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 112
                Layout.preferredHeight: 112
                account: page.account
                jid: page.jid
                label: page.displayName !== "" ? page.displayName : page.jid
                initialsPixelSize: 40
            }

            Card {
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Caption { text: qsTr("XMPP address") }
                    Text {
                        objectName: "contactJid"
                        Layout.fillWidth: true
                        text: page.jid
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Caption { text: qsTr("Name") }
                    Text {
                        objectName: "contactName"
                        Layout.fillWidth: true
                        text: page.displayName !== "" ? page.displayName : qsTr("Not set")
                        color: page.displayName !== "" ? Theme.textPrimary
                                                       : Theme.textDim
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }

                // Two facts rather than one: whether they are a contact at all,
                // and what the two of you have agreed to share.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        objectName: "contactStanding"
                        Layout.fillWidth: true
                        text: page.rostered ? qsTr("In your contacts")
                                            : qsTr("Not in your contacts")
                        color: Theme.textPrimary
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                    Caption {
                        objectName: "contactSharing"
                        Layout.fillWidth: true
                        text: page.rostered
                              ? page.sharingNote
                              : qsTr("You have a conversation with them but have never added them.")
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Card {
                Text {
                    text: qsTr("Their devices")
                    color: Theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("Compare a key with them over another channel before trusting it.")
                    wrapMode: Text.WordWrap
                }

                // Nothing arrives for a contact until their device list does,
                // and none of it arrives before the account has connected.
                Caption {
                    Layout.fillWidth: true
                    objectName: "noKeysNotice"
                    visible: theirKeys.count === 0
                    text: qsTr("No keys for this contact yet. They appear once their devices announce themselves.")
                    wrapMode: Text.WordWrap
                }

                TrustList {
                    devices: theirKeys
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }
            }

            // Verifying runs both ways, so the key they will be reading back
            // to you belongs on the same screen.
            Card {
                Text {
                    text: qsTr("This device")
                    color: Theme.textPrimary
                    font.pixelSize: 18
                    font.bold: true
                }
                Fingerprint {
                    Layout.fillWidth: true
                    objectName: "ownFingerprint"
                    visible: ownKey.ownFingerprint !== ""
                    hex: ownKey.ownFingerprint
                    note: qsTr("yours")
                    onCopyRequested: (spaced) => page.copyFingerprint(spaced)
                }
                Caption {
                    Layout.fillWidth: true
                    visible: ownKey.ownFingerprint === ""
                    text: qsTr("This device gets its key once the account has connected.")
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    CopiedNotice {
        id: copiedNotice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        text: qsTr("Fingerprint copied")
    }
}
