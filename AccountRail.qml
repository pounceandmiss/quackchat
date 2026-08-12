pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Discord/Element-style rail: one avatar per account, bound to App.accounts.
//
// Two densities from the one component: compact (the default) is the 64px strip
// beside the conversations list, expanded is what the narrow layout's drawer
// shows, where there is room for the JID, the connection state and a
// per-account menu button.
Rectangle {
    id: rail
    objectName: "accountRail"
    property string currentAccount: ""
    property bool expanded: false
    signal selectAccount(string jid)

    // Its own fill rather than Theme.background: beside a near-white chat list
    // the app background is a percent or two off and the two read as one pane.
    color: Theme.rail
    implicitWidth: expanded ? 280 : 64

    // In the drawer this spans the window, system bars included; inline it sits
    // within the shell, which ShellWindow has already kept clear - so 0 there.
    readonly property real topInset: SafeArea.margins.top
    readonly property real bottomInset: SafeArea.margins.bottom

    // The rail's own edge as a column; in the drawer, the drawer draws it.
    Rectangle {
        anchors.right: parent.right
        width: 1; height: parent.height
        color: Theme.hairline
        visible: !rail.expanded
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

    // The same states in words, for the expanded rows: one red dot cannot say
    // whether the server is unreachable or the password was rejected.
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
            spacing: rail.expanded ? 2 : 8

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
                height: rail.expanded ? 64 : 56

                readonly property bool current: cell.jid === rail.currentAccount

                // Expanded rows have the width to take a full wash; compact has
                // only the accent tab to mark the current account with.
                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 8
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    radius: 10
                    color: Theme.menuHover
                    visible: rail.expanded && cell.current
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

                // Circle that squares off into a rounded tile while current.
                Avatar {
                    id: badge
                    x: rail.expanded ? 16 : (cell.width - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
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
                    // Punches the dot out of the rail it sits on, so it tracks
                    // the rail's fill rather than the window's.
                    border.color: Theme.rail
                }

                // Compact reaches this menu by right-click or long press, which
                // an expanded row has the width to spell out as a button.
                IconButton {
                    id: moreBtn
                    objectName: "accountRowMenuButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: rail.expanded
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
                    visible: rail.expanded

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
                        color: rail.stateColor(cell.connState, cell.acctEnabled)
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
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

        // Expanded only: in the compact strip the ＋ is one more circle in the
        // column, and a rule above it would read as a break in the list.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
            visible: rail.expanded
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
                    x: rail.expanded ? 16 : (parent.width - width) / 2
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
                    visible: rail.expanded
                    text: "Add account"
                    color: Theme.textPrimary
                    font.pixelSize: 14
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
