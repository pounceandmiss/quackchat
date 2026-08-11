pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// A ringing call, before it is a call: who is calling, and the two answers.
// As in tacky's Tk GUI, an <Incoming> raises this rather than a call window -
// the window only exists once there is a call to show in it. Small and
// differently shaped on purpose: call both ends of a call from this app and the
// two would otherwise be lookalike windows on the same spot.
//
// AppWindows shows it while the row is ringing, so it goes by itself however
// that ends - answered here, answered on another device, or retracted.
ApplicationWindow {
    id: dlg

    required property string sid
    required property string account
    required property string peer

    width: 340
    height: 250
    minimumWidth: 300
    minimumHeight: 220
    title: qsTr("Incoming Call")
    color: Theme.background

    // Escape and the window button both decline, as in the Tk GUI: there is no
    // way to put a ringing call aside without answering it either way.
    Shortcut { sequence: "Escape"; onActivated: App.calls.reject(dlg.account, dlg.sid) }
    onClosing: App.calls.reject(dlg.account, dlg.sid)

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
            jid: dlg.peer
            label: dlg.peer
            initialsPixelSize: 22
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 10
            text: qsTr("Incoming call from")
            color: Theme.textDim
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 2
            text: dlg.peer
            color: Theme.textPrimary
            font.pixelSize: 16
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12
            spacing: 16

            Item { Layout.fillWidth: true }

            CallButton {
                symbol: "call"
                fill: Theme.positive
                text: qsTr("Answer")
                onClicked: App.calls.accept(dlg.account, dlg.sid)
            }

            CallButton {
                symbol: "call-end"
                fill: Theme.negative
                text: qsTr("Decline")
                onClicked: App.calls.reject(dlg.account, dlg.sid)
            }

            Item { Layout.fillWidth: true }
        }
    }
}
