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
    // Set in the narrow layout, where the account rail is a drawer rather than
    // a column of its own and this header is the way to it.
    property bool showAccounts: false
    signal openChat(string jid, string name, bool groupchat)
    signal popOutChat(string jid, string name, bool groupchat)
    signal startSearch()
    signal openAccounts()
    background: Rectangle { color: Theme.surface }

    header: Rectangle {
        height: 60
        color: Theme.surface
        RowLayout {
            anchors.fill: parent
            // The glyph button carries its own padding, so the text lines up
            // with the title either way.
            anchors.leftMargin: page.showAccounts ? 4 : 16
            anchors.rightMargin: 8
            spacing: 8
            IconButton {
                objectName: "accountsButton"
                iconPath: Icons.menu
                Accessible.name: qsTr("Accounts")
                glyphColor: Theme.textDim
                visible: page.showAccounts
                onClicked: page.openAccounts()
            }
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
                iconPath: Icons.moreHoriz
                Accessible.name: qsTr("More")
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
        objectName: "chatList"
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
            required property int unread
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

                // The Tk list's "Name (3)" suffix, as the badge an avatar row
                // has room for. Hidden at zero, so the layout skips it.
                Rectangle {
                    objectName: "unreadBadge"
                    Layout.rightMargin: 14
                    visible: row.unread > 0
                    implicitHeight: 20
                    implicitWidth: Math.max(height, unreadText.implicitWidth + 12)
                    radius: height / 2
                    color: Theme.accent

                    Text {
                        id: unreadText
                        objectName: "unreadCount"
                        anchors.centerIn: parent
                        // tacky counts the true total; three digits of it would
                        // eat the name.
                        text: row.unread > 99 ? "99+" : row.unread.toString()
                        color: Theme.textOnAccent
                        font.pixelSize: 12
                        font.bold: true
                    }
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
                return "No account selected.\nUse + on the left to add one."
            // connRev is read purely to give this binding a dependency:
            // connStateFor is a call, so nothing would re-run it otherwise.
            App.accounts.connRev
            // A load that failed leaves the list as empty as one that succeeded
            // with nothing in it, so say which happened.
            const failure = listView.model ? listView.model.loadError : ""
            if (failure !== "")
                return "Couldn't load conversations.\n" + failure
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
