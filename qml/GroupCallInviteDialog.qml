pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Someone calling a chat of ours: an XEP-0482 invite to a group call,
// arriving live. The invite is kept in the chat as a card either way; this
// is the ring, and answering here answers that card.
ApplicationWindow {
    id: dlg
    objectName: "groupCallInviteDialog"

    required property string account
    // The chat the invite is stored in (its message's chat JID) and when.
    required property string chat
    required property var timestamp
    required property string room
    required property string from
    required property bool video

    readonly property string chatName: chat.replace(/\?join$/, "")
    readonly property GroupCall call: App.groupCalls.callFor(account, chat)

    width: 360
    height: 240
    minimumWidth: 320
    minimumHeight: 220
    title: qsTr("Incoming group call")
    color: Theme.background
    // Spawned by AppWindows on the event, so nothing else shows it.
    visible: true

    Shortcut { sequence: "Escape"; onActivated: dlg.close() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.topMargin: 16 + SafeArea.margins.top
        anchors.bottomMargin: 16 + SafeArea.margins.bottom
        spacing: 0

        Item { Layout.fillHeight: true }

        Avatar {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            account: dlg.account
            jid: dlg.from
            label: dlg.from !== "" ? dlg.from : dlg.chatName
            initialsPixelSize: 22
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 10
            text: dlg.from !== "" ? dlg.from : qsTr("Someone")
            color: Theme.textPrimary
            font.pixelSize: 16
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 2
            text: dlg.video ? qsTr("is starting a group video call in %1").arg(dlg.chatName)
                            : qsTr("is starting a group call in %1").arg(dlg.chatName)
            color: Theme.textDim
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 24

            Item { Layout.fillWidth: true }

            CallButton {
                objectName: "groupCallInviteDecline"
                iconPath: Icons.callEnd
                fill: Theme.negative
                glyphColor: "#ffffff"
                text: qsTr("Decline")
                onClicked: {
                    if (dlg.call)
                        dlg.call.declineInvite(dlg.chat, dlg.timestamp)
                    dlg.close()
                }
            }
            CallButton {
                objectName: "groupCallInviteJoin"
                iconPath: Icons.call
                fill: Theme.positive
                glyphColor: "#ffffff"
                text: qsTr("Join")
                onClicked: {
                    if (dlg.call)
                        dlg.call.answerInvite(dlg.chat, dlg.timestamp, dlg.room, dlg.video)
                    dlg.close()
                }
            }

            Item { Layout.fillWidth: true }
        }
    }
}
