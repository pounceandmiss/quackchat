pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The message half of the conversations list's search, sized to its own rows so
// the list it sits at the foot of scrolls both halves as one.
//
// Results are deliberately not chat content - a hit is a pointer to a message,
// and opening it hands the chat the jump. Searching inside one chat is the
// chat's own affair; see ChatPage.
//
// Naming no chat makes this local-only, which is tacky's rule rather than a
// choice: MAM queries one archive, so a remote search needs one to query.
Column {
    id: section
    objectName: "messageHits"
    property string account: ""
    property string query: ""
    // What the same query matched by name above, which words the empty answer:
    // with nothing there either, this line speaks for the search as a whole.
    property int chatMatches: 0

    // matches travels with the hit so the chat can mark the same run this row
    // marks, without asking what the query was.
    signal openHit(string chatJid, real ts, var matches)

    SearchModel {
        id: results
        backend: App.backend
        account: section.account
        query: section.query
    }

    // Long enough that a burst of typing is one search. It need not be longer:
    // an answer landing mid-word replaces the last one where it stands.
    Timer {
        id: debounce
        interval: 150
        onTriggered: results.search()
    }

    // Emptying the box ends the search rather than searching for nothing, which
    // would leave hits under a list already back to showing every chat.
    onQueryChanged: {
        if (section.query === "") {
            debounce.stop()
            results.clear()
        } else {
            debounce.restart()
        }
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
            account: section.account
            chat: modelData
        }
        onObjectAdded: section.rebuildAuthors()
        onObjectRemoved: section.rebuildAuthors()
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
        section.authorsByChat = next
    }

    // Until the map lands the JID stands in for the name, which is what tacky
    // falls back to anyway.
    function authorName(chat, jid) {
        if (jid === "")
            return ""
        if (jid === section.account)
            return "You"
        const names = section.authorsByChat[chat]
        const known = names ? names.names[jid] : undefined
        return known !== undefined && known !== "" ? known : jid
    }

    readonly property ChatListModel chatList:
        section.account !== "" ? App.chatListFor(section.account) : null

    // Which conversation a hit came from. The list has a name for the chats it
    // knows; the rest show as the JID they are.
    function chatLabel(jid) {
        const name = section.chatList ? section.chatList.entryFor(jid).name : undefined
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

    SectionLabel {
        width: section.width
        text: "Messages"
        shown: results.count > 0
    }

    // A Repeater rather than a list of its own: this is the foot of a list
    // already, and two scrolling areas would fight over the same flick.
    Repeater {
        model: results

        delegate: ItemDelegate {
            id: hit
            required property real timestamp
            required property string chatJid
            required property string from
            required property string snippet
            required property var matches

            width: section.width
            height: 62
            onClicked: section.openHit(hit.chatJid, hit.timestamp, hit.matches)

            background: Rectangle {
                color: hit.hovered ? Theme.menuHover : "transparent"
            }

            contentItem: ColumnLayout {
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text {
                        text: section.authorName(hit.chatJid, hit.from)
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
                        text: section.chatLabel(hit.chatJid)
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        text: section.fmtWhen(hit.timestamp)
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
    }

    // One line for every state the rows cannot show, and silent until the first
    // search is out, so a half-typed word does not answer for itself.
    Item {
        width: section.width
        height: 44
        visible: status.text !== "" && results.count === 0

        Text {
            id: status
            objectName: "searchStatus"
            anchors.centerIn: parent
            width: parent.width - 40
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            color: Theme.textDim
            font.pixelSize: 13
            text: {
                if (results.searching)
                    return "Searching messages…"
                if (results.failed)
                    return "The message search failed."
                if (results.searched)
                    return section.chatMatches > 0
                         ? "No messages match."
                         : "Nothing matches “" + section.query + "”."
                return ""
            }
        }
    }

    // Paging is a button rather than a scroll trigger: a page is 30 hits and
    // the archive can be deep, so walking it is the user's call.
    Item {
        width: section.width
        height: 52
        visible: results.count > 0 && !results.complete

        Button {
            objectName: "loadMore"
            anchors.centerIn: parent
            text: results.searching ? "Searching…" : "Load more"
            enabled: !results.searching
            onClicked: results.loadMore()
        }
    }
}
