pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Join a MUC, by typing its address or by asking a service what it hosts: the
// Tk GUI's joinroomdialog. Joining is a bookmark write with autojoin set - the
// same thing the row menu's "Join" tick does - because membership is what makes
// tacky keep the room joined across reconnects.
//
// Unlike the Tk dialog it does not wait for the join to succeed: the room's row
// appears in the list at once and carries the outcome (joining, then joined or
// an error with its reason), so there is nothing here left to report.
SheetDialog {
    id: sheet
    objectName: "joinRoomSheet"

    property string account: ""
    signal joinRoom(string jid, string nick, string password)

    title: "Join room"
    preferredWidth: 420
    height: Math.min(480, parent ? parent.height - 24 : 480)
    standardButtons: Dialog.Cancel | Dialog.Ok

    // The list's rooms carry a ?join suffix; the bookmark commands are keyed by
    // the bare room JID, so it goes in without one either way.
    readonly property string targetJid: Jid.bare(roomField.text)
    readonly property bool canJoin:
        Jid.plausible(targetJid) && nickField.text.trim() !== ""

    // Where a server's rooms live by convention. A guess, and editable: plenty
    // of deployments put theirs somewhere else.
    function defaultService() {
        const at = sheet.account.indexOf("@")
        return at < 0 ? "" : "conference." + sheet.account.substring(at + 1)
    }

    MucRoomsModel {
        id: rooms
        backend: App.backend
        account: sheet.account
    }

    onAboutToShow: {
        roomField.clear()
        passwordField.clear()
        serviceField.text = sheet.defaultService()
        rooms.clear()
        nickField.text = rooms.defaultNick
        rooms.requestDefaultNick()
        roomField.forceActiveFocus()
    }
    // The nick is a round trip, so it lands after the dialog is already up -
    // filled in only if nobody has typed one of their own by then.
    Connections {
        target: rooms
        function onDefaultNickChanged() {
            if (nickField.text === "")
                nickField.text = rooms.defaultNick
        }
    }

    onAccepted: sheet.joinRoom(sheet.targetJid, nickField.text.trim(),
                               passwordField.text)

    Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(() => sheet.canJoin)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 6

            Label {
                text: "Room"
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                id: roomField
                objectName: "joinRoomJid"
                Layout.fillWidth: true
                placeholderText: "room@conference.example.com"
                inputMethodHints: Qt.ImhNoAutoUppercase
            }

            Label {
                text: "Nickname"
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                id: nickField
                objectName: "joinRoomNick"
                Layout.fillWidth: true
                placeholderText: "how the room sees you"
            }

            Label {
                text: "Password"
                color: Theme.textDim
                font.pixelSize: 12
            }
            TextField {
                id: passwordField
                objectName: "joinRoomPassword"
                Layout.fillWidth: true
                placeholderText: "only if the room asks"
                echoMode: TextInput.Password
            }

            Label {
                text: "Service"
                color: Theme.textDim
                font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                TextField {
                    id: serviceField
                    objectName: "joinRoomService"
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    onAccepted: rooms.discover(text.trim())
                }
                Button {
                    objectName: "discoverButton"
                    text: rooms.loading ? "…" : "Discover"
                    enabled: !rooms.loading && serviceField.text.trim() !== ""
                    onClicked: rooms.discover(serviceField.text.trim())
                }
            }
        }

        // The discovered rooms. Server order: what a service lists and in what
        // sequence is its answer, not ours to rearrange.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.field
            radius: 6
            border.color: Theme.hairline

            ListView {
                id: roomList
                objectName: "discoveredRooms"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: rooms
                ScrollBar.vertical: ScrollBar {}

                delegate: ItemDelegate {
                    id: roomRow
                    required property string jid
                    required property string name
                    required property int occupants
                    width: ListView.view.width
                    height: 40
                    // Picking a room fills the box rather than joining outright:
                    // the nick and password above still apply to it.
                    onClicked: roomField.text = roomRow.jid

                    background: Rectangle {
                        color: roomRow.hovered ? Theme.menuHover : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: roomRow.name !== "" ? roomRow.name : roomRow.jid
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: roomRow.jid
                                visible: roomRow.name !== ""
                                color: Theme.textDim
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            visible: roomRow.occupants > 0
                            spacing: 3
                            Text {
                                text: roomRow.occupants
                                color: Theme.textDim
                                font.pixelSize: 12
                            }
                            Glyph {
                                path: Icons.group
                                color: Theme.textDim
                                size: 14
                            }
                        }
                    }
                }
            }

            // Standing in for the list: nothing discovered yet, a service that
            // hosts nothing, or one that would not say.
            Text {
                objectName: "discoverHint"
                anchors.centerIn: parent
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: roomList.count === 0
                color: Theme.textDim
                font.pixelSize: 12
                text: {
                    if (rooms.loading)
                        return "Asking " + serviceField.text.trim() + " …"
                    if (rooms.error !== "")
                        return "Couldn't list rooms.\n" + rooms.error
                    if (rooms.loaded)
                        return "That service lists no rooms."
                    return "Type a room address, or Discover what a service hosts."
                }
            }
        }
    }
}
