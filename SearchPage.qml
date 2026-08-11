pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Search across every conversation on one account, standing in for the
// conversations list while it is open. Results are deliberately not chat
// content - a hit is a pointer to a message, and opening it hands the chat the
// jump. Searching inside one chat is the chat's own affair; see ChatPage.
//
// Naming no chat makes this local-only, which is tacky's rule rather than a
// choice: MAM queries one archive, so a remote search needs one to query.
Page {
    id: page
    objectName: "searchPane"
    property string account: ""

    // matches travels with the hit so the chat can mark the same run this row
    // marks, without asking what the query was.
    signal openHit(string chatJid, real ts, var matches)
    signal closed()

    background: Rectangle { color: Theme.surface }

    // Closing the pane ends the search.
    onVisibleChanged: if (!page.visible) results.clear()

    function focusInput() {
        input.forceActiveFocus()
        input.selectAll()
    }

    SearchModel {
        id: results
        backend: App.backend
        account: page.account
        query: input.text
    }

    // Long enough that a burst of typing is one search, short enough that a
    // pause answers straight away.
    Timer {
        id: debounce
        interval: 250
        onTriggered: results.search()
    }

    // A name cache is per chat, so an account-wide result set needs one for
    // each chat it touches. Rebuilt as a fresh object so the delegates reading
    // it re-evaluate; the names inside it notify on their own.
    property var authorsByChat: ({})
    Instantiator {
        id: authorPool
        model: results.resultChats
        delegate: AuthorNames {
            required property string modelData
            backend: App.backend
            account: page.account
            chat: modelData
        }
        onObjectAdded: page.rebuildAuthors()
        onObjectRemoved: page.rebuildAuthors()
    }
    // Keyed off the model rather than the objects: objectAt follows the model's
    // own order, and the objects come back as bare QObjects.
    function rebuildAuthors() {
        const chats = results.resultChats
        const next = ({})
        for (let i = 0; i < chats.length && i < authorPool.count; ++i) {
            const a = authorPool.objectAt(i)
            if (a)
                next[chats[i]] = a
        }
        page.authorsByChat = next
    }

    // Until the map lands the JID stands in for the name, which is what tacky
    // falls back to anyway.
    function authorName(chat, jid) {
        if (jid === "")
            return ""
        if (jid === page.account)
            return "You"
        const names = page.authorsByChat[chat]
        const known = names ? names.names[jid] : undefined
        return known !== undefined && known !== "" ? known : jid
    }

    readonly property ChatListModel chatList:
        page.account !== "" ? App.chatListFor(page.account) : null

    // Which conversation a hit came from. The list has a name for the chats it
    // knows; the rest show as the JID they are.
    function chatLabel(jid) {
        const name = page.chatList ? page.chatList.entryFor(jid).name : undefined
        return name !== undefined && name !== "" ? name : jid
    }

    function fmtWhen(ts) {
        if (!ts) return ""
        const d = new Date(ts / 1000) // tacky timestamps are microseconds
        const hm = ("0" + d.getHours()).slice(-2) + ":" + ("0" + d.getMinutes()).slice(-2)
        const today = new Date()
        if (d.toDateString() === today.toDateString())
            return hm
        return d.toLocaleDateString(Qt.locale(), Locale.ShortFormat) + " " + hm
    }

    header: Rectangle {
        height: 60
        color: Theme.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 10
            spacing: 6

            IconButton {
                text: "‹"
                font.pixelSize: 28
                onClicked: page.closed()
            }
            TextField {
                id: input
                objectName: "searchField"
                Layout.fillWidth: true
                placeholderText: "Search all chats"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextChanged: {
                    if (input.text === "") {
                        debounce.stop()
                        results.clear()
                    } else {
                        debounce.restart()
                    }
                }
                // Nothing waits on Enter; it only skips what is left of the
                // pause.
                onAccepted: {
                    debounce.stop()
                    results.search()
                }
                Keys.onEscapePressed: page.closed()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width; height: 1
            color: Theme.hairline
        }
    }

    ListView {
        id: hits
        objectName: "searchResults"
        anchors.fill: parent
        model: results
        clip: true

        ScrollBar.vertical: ScrollBar {
            id: hitScroll
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: Theme.textDim
                visible: hitScroll.size < 1
                opacity: hitScroll.pressed ? 0.8 : hitScroll.active ? 0.5 : 0.25
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }

        delegate: ItemDelegate {
            id: hit
            required property real timestamp
            required property string chatJid
            required property string from
            required property string snippet
            required property var matches

            width: ListView.view.width
            height: 62
            onClicked: page.openHit(hit.chatJid, hit.timestamp, hit.matches)

            background: Rectangle {
                color: hit.hovered ? Theme.menuHover : "transparent"
            }

            contentItem: ColumnLayout {
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: page.authorName(hit.chatJid, hit.from)
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        // Off the delegate, not the row: the row is sized by
                        // what its children ask for, so measuring it here to
                        // decide what to ask for is a loop.
                        Layout.maximumWidth: hit.width * 0.5
                    }
                    Text {
                        Layout.fillWidth: true
                        // The list mixes conversations, so each row has to say
                        // which one it came from.
                        text: page.chatLabel(hit.chatJid)
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        text: page.fmtWhen(hit.timestamp)
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: hit.snippet
                    textFormat: Text.RichText
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
            }
        }

        // Paging is a button rather than a scroll trigger: a page is 30 hits
        // and the archive can be deep, so walking it is the user's call.
        footer: Item {
            width: hits.width
            height: results.complete ? 0 : 52
            visible: !results.complete && hits.count > 0
            Button {
                objectName: "loadMore"
                anchors.centerIn: parent
                text: results.searching ? "Searching…" : "Load more"
                enabled: !results.searching
                onClicked: results.loadMore()
            }
        }
    }

    // One line for every state the list itself cannot show.
    Text {
        objectName: "searchStatus"
        anchors.centerIn: parent
        width: parent.width - 60
        visible: text !== "" && hits.count === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: 14
        text: {
            if (results.searching)
                return "Searching…"
            if (results.failed)
                return "The search failed."
            if (results.searched)
                return "No messages match."
            return "Search every conversation on this account."
        }
    }
}
