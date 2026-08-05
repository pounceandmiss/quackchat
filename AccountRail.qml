pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Discord/Element-style rail: one avatar per account, bound to App.accounts.
Rectangle {
    id: rail
    objectName: "accountRail"
    property string currentAccount: ""
    signal selectAccount(string jid)

    color: Theme.background
    implicitWidth: 64

    Rectangle {
        anchors.right: parent.right
        width: 1; height: parent.height
        color: Theme.hairline
    }

    // Android and iOS are single-window, so there the details get a full-screen
    // sheet over the shell instead of a window of their own.
    function openSettings(jid) {
        if (Theme.mobile) {
            settingsSheet.account = jid
            settingsSheet.open()
        } else {
            AppWindows.accountSettings(jid)
        }
    }

    // Maps a connState string to the status-dot color.
    function stateColor(state, enabled) {
        if (!enabled)
            return Theme.textDim
        switch (state) {
        case "connected": return Theme.positive
        case "auth-error":
        case "conn-error": return Theme.negative
        case "waiting":
        case "disconnected": return Theme.warning
        case "": return Theme.textDim
        default: return Theme.accent2 // connecting / authenticating / binding
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: list
            objectName: "accountRailList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.accounts
            clip: true
            topMargin: 10
            spacing: 8

            delegate: Item {
                id: cell
                // Read via `model` rather than a `required property bool enabled`,
                // which would shadow Item.enabled and disable the delegate's own
                // input when the account is disabled (you could never re-enable it).
                required property var model
                readonly property string jid: cell.model.jid
                readonly property string connState: cell.model.connState
                readonly property bool acctEnabled: cell.model.enabled
                width: ListView.view.width
                height: 56

                readonly property bool current: cell.jid === rail.currentAccount

                // Tall accent tab for the current account, a small nub otherwise.
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: -1
                    width: 5
                    height: cell.current ? 40 : 0
                    radius: 3
                    color: Theme.accentDeep
                    visible: height > 0
                    Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }

                // Circle that squares off into a rounded tile while current.
                Avatar {
                    id: badge
                    anchors.centerIn: parent
                    width: 44; height: 44
                    radius: cell.current ? 12 : 22
                    opacity: cell.acctEnabled ? 1.0 : 0.45
                    Behavior on radius { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    account: cell.jid
                    jid: cell.jid
                    label: cell.jid
                }

                Rectangle {
                    anchors.right: badge.right
                    anchors.bottom: badge.bottom
                    width: 13; height: 13
                    radius: 6.5
                    color: rail.stateColor(cell.connState, cell.acctEnabled)
                    border.width: 2
                    border.color: Theme.background
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: rail.selectAccount(cell.jid)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: ctx.popup()
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onLongPressed: ctx.popup()
                }

                Menu {
                    id: ctx
                    MenuItem {
                        text: "Account details…"
                        onTriggered: rail.openSettings(cell.jid)
                    }
                    MenuItem {
                        text: cell.acctEnabled ? "Disable" : "Enable"
                        onTriggered: cell.acctEnabled ? App.accounts.disable(cell.jid)
                                                      : App.accounts.enable(cell.jid)
                    }
                    MenuItem {
                        text: "Remove…"
                        onTriggered: removeConfirm.open()
                    }
                }

                // A destructive action (drops the account + its cache), so confirm.
                Dialog {
                    id: removeConfirm
                    anchors.centerIn: Overlay.overlay
                    modal: true
                    title: "Remove account"
                    standardButtons: Dialog.Cancel | Dialog.Yes
                    onAccepted: App.accounts.remove(cell.jid)
                    Text {
                        text: "Remove " + cell.jid + "?\nThis deletes its local cache."
                        color: Theme.textPrimary
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        ToolButton {
            id: addBtn
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            onClicked: addSheet.open()
            contentItem: Rectangle {
                anchors.centerIn: parent
                width: 44; height: 44
                radius: 22
                color: addBtn.hovered ? Theme.menuHover : Theme.field
                border.width: 1
                border.color: Theme.hairline
                Text {
                    anchors.centerIn: parent
                    text: "＋"
                    color: Theme.accent
                    font.pixelSize: 22
                }
            }
            background: Item {}
        }
    }

    AddAccountSheet { id: addSheet }

    // Full-screen rather than a centred dialog: the form plus a device list of
    // unknown length is a screenful.
    Dialog {
        id: settingsSheet
        property alias account: settingsPage.account
        parent: Overlay.overlay
        modal: true
        padding: 0
        x: 0
        y: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
        // The sheet outlives each visit, so start from what is stored rather
        // than from whatever was typed and abandoned last time.
        onAboutToShow: settingsPage.bindAccount()

        AccountSettingsPage {
            id: settingsPage
            anchors.fill: parent
            onDone: settingsSheet.close()
        }
    }
}
