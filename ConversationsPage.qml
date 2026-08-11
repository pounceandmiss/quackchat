pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The conversations list for one account. `account` selects which per-account
// ChatListModel to bind (App.chatListFor); an empty account yields an empty list.
Page {
    id: page
    objectName: "conversationsPane"
    property string account: ""
    signal openChat(string jid, string name, bool groupchat)
    signal popOutChat(string jid, string name, bool groupchat)
    signal startSearch()
    background: Rectangle { color: Theme.surface }

    header: Rectangle {
        height: 60
        color: Theme.surface
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: "Chats"
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: page.account
                    visible: page.account !== ""
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            IconButton {
                objectName: "searchButton"
                text: "🔍"
                font.pixelSize: 16
                glyphColor: Theme.textDim
                enabled: page.account !== ""
                opacity: enabled ? 1 : 0.4
                onClicked: page.startSearch()
            }
            IconButton {
                id: overflowBtn
                text: "⋯"
                font.pixelSize: 20
                glyphColor: Theme.textDim
                onClicked: overflow.popup(overflowBtn,
                                          overflowBtn.width - overflow.width,
                                          overflowBtn.height + 2)
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: Theme.hairline
        }
    }

    // Window-level actions, previously a global toolbar. The keyboard shortcuts
    // (Ctrl+N / Ctrl+T) live on the window; this menu is their mouse path.
    Menu {
        id: overflow
        width: 190
        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.hairline
        }
        component OverflowEntry: MenuItem {
            id: mi
            height: 40
            property string shortcutHint: ""
            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: mi.text
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                Text {
                    text: mi.shortcutHint
                    color: Theme.textDim
                    font.pixelSize: 12
                    rightPadding: 8
                }
            }
            background: Rectangle {
                color: mi.highlighted ? Theme.menuHover : "transparent"
                radius: 6
            }
        }
        OverflowEntry {
            text: "New window"
            shortcutHint: "Ctrl+N"
            // Single-window platforms: hide, with height 0 so the menu's
            // column doesn't hold a blank slot for the invisible item.
            visible: !Theme.mobile
            height: Theme.mobile ? 0 : 40
            onTriggered: AppWindows.newShell(page.account)
        }
        OverflowEntry {
            text: "Change theme"
            shortcutHint: "Ctrl+T"
            onTriggered: Theme.cycle()
        }
    }

    ListView {
        id: listView
        anchors.fill: parent
        model: page.account !== "" ? App.chatListFor(page.account) : null
        clip: true

        ScrollBar.vertical: ScrollBar {
            id: listScroll
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: Theme.textDim
                visible: listScroll.size < 1
                opacity: listScroll.pressed ? 0.8 : listScroll.active ? 0.5 : 0.25
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }

        delegate: ItemDelegate {
            id: row
            required property string jid
            required property string name
            required property bool groupchat
            width: ListView.view.width
            height: 64
            onClicked: page.openChat(jid, name, groupchat)

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: page.popOutChat(row.jid, row.title, row.groupchat)
            }

            readonly property string title: name !== "" ? name : jid

            background: Rectangle {
                color: row.hovered ? Theme.menuHover : "transparent"
            }

            contentItem: RowLayout {
                spacing: 12

                Avatar {
                    Layout.leftMargin: 14
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44
                    account: page.account
                    jid: row.jid
                    label: row.title
                    initialsPixelSize: 16
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: row.title
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: row.jid
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                Text {
                    Layout.rightMargin: 14
                    text: row.groupchat ? "👥" : ""
                    font.pixelSize: 14
                }
            }
        }
    }

    // Empty state, reflecting the real connection state (not just "empty").
    Text {
        objectName: "emptyHint"
        anchors.centerIn: parent
        visible: listView.count === 0
        width: parent.width - 60
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: 14
        text: {
            if (page.account === "")
                return "No account selected.\nUse ＋ on the left to add one."
            switch (App.accounts.connStateFor(page.account)) {
            case "connected":
                return "Connected as " + page.account + ".\nNo conversations yet."
            case "auth-error":
                return "Authentication failed for " + page.account + ".\nCheck the password."
            case "conn-error":
                return "Can't reach the server for " + page.account + ".\nRetrying…"
            case "disconnected":
            case "waiting":
                return "Reconnecting " + page.account + " …"
            default: // starting, connecting, authenticating, binding
                return "Connecting " + page.account + " …"
            }
        }
    }
}
