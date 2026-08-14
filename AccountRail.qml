pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One row per account, bound to App.accounts: avatar, JID, connection state and
// a per-account menu button. Lives in the shell's drawer rather than beside the
// conversations list - a column narrow enough to leave permanently has room for
// avatars and nothing else.
Rectangle {
    id: rail
    objectName: "accountRail"
    property string currentAccount: ""
    signal selectAccount(string jid)

    // Its own fill rather than Theme.background: over a near-white chat list
    // the app background is a percent or two off and the two read as one pane.
    color: Theme.rail
    implicitWidth: 280

    // The drawer spans the window, system bars included.
    readonly property real topInset: SafeArea.margins.top
    readonly property real bottomInset: SafeArea.margins.bottom

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

    // The state in words beside the badge's dot: one red dot cannot say whether
    // the server is unreachable or the password was rejected.
    function stateText(state, enabled) {
        if (!enabled)
            return "disabled"
        switch (state) {
        case "connected": return "connected"
        case "auth-error": return "sign-in failed"
        case "conn-error": return "connection failed"
        case "waiting": return "reconnecting…"
        case "disconnected":
        case "": return "offline"
        default: return state // connecting / authenticating / binding
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
            topMargin: 10 + rail.topInset
            spacing: 2

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
                height: 64

                readonly property bool current: cell.jid === rail.currentAccount

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 8
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    radius: 10
                    color: Theme.menuHover
                    visible: cell.current
                }

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

                AccountBadge {
                    id: badge
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    jid: cell.jid
                    connState: cell.connState
                    acctEnabled: cell.acctEnabled
                    current: cell.current
                }

                IconButton {
                    id: moreBtn
                    objectName: "accountRowMenuButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    iconPath: Icons.moreHoriz
                    Accessible.name: qsTr("Account actions")
                    glyphColor: Theme.textDim
                    onClicked: ctx.popup(moreBtn, 0, moreBtn.height)
                }

                ColumnLayout {
                    anchors.left: badge.right
                    anchors.leftMargin: 12
                    anchors.right: moreBtn.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    Text {
                        objectName: "accountRowJid"
                        Layout.fillWidth: true
                        text: cell.jid
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: cell.current
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "accountRowState"
                        Layout.fillWidth: true
                        text: rail.stateText(cell.connState, cell.acctEnabled)
                        color: badge.stateColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: rail.selectAccount(cell.jid)
                }
                // A touch point carries no button for acceptedButtons to
                // filter; touch has the long press below.
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
        }

        ToolButton {
            id: addBtn
            objectName: "addAccountButton"
            Layout.fillWidth: true
            // The inset is padding below the button, not a taller target: the
            // circle stays centred in the 60 above it.
            Layout.preferredHeight: 60 + rail.bottomInset
            Accessible.name: qsTr("Add account")
            onClicked: addSheet.open()
            contentItem: Item {
                Rectangle {
                    id: plus
                    x: 16
                    y: (parent.height - rail.bottomInset - height) / 2
                    width: 44; height: 44
                    radius: 22
                    color: addBtn.hovered ? Theme.menuHover : Theme.field
                    border.width: 1
                    border.color: Theme.hairline
                    Glyph {
                        anchors.centerIn: parent
                        path: Icons.add
                        color: Theme.accent
                        size: 24
                    }
                }
                Text {
                    anchors.left: plus.right
                    anchors.leftMargin: 12
                    anchors.verticalCenter: plus.verticalCenter
                    text: "Add account"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                }
            }
            background: Item {}
        }
    }

    AddAccountSheet { id: addSheet; objectName: "addAccountSheet" }

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
