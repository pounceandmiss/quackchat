pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Everything one room is: who is in it, what it is about, and what this account
// may do about either. ContactDetailsPage for the other kind of chat, and
// hosted the same two ways - a window on desktop, a full-screen sheet on
// mobile. The Tk GUI spreads this over a participant sidebar, a legend toplevel
// and half the Chat menu.
//
// The whole page is a single ListView, so the room card scrolls away with the
// people rather than pinning a card over a list of two. Its filter takes over
// the header the way the chat's search does, which keeps it reachable from
// anywhere in a long room.
Page {
    id: page
    objectName: "mucDetailsPage"

    property string account: ""
    // The chat JID, ?join suffix and all - the model cuts it down to the room.
    property string jid: ""
    property string name: ""
    property bool showClose: true
    signal done

    readonly property string roomTitle: name !== "" ? name : room.roomJid

    background: Rectangle { color: Theme.background }

    MucRoomModel {
        id: room
        objectName: "mucRoom"
        backend: App.backend
        account: page.account
        jid: page.jid
        // Only while the box is up: a filter still narrowing the list from a
        // header nobody can see would read as a room that had emptied.
        filter: page.filterMode ? filterInput.text : ""
        onActionFailed: (action, message) => {
            actionError.message = qsTr("%1 failed: %2").arg(action).arg(message)
            actionError.open()
        }
    }

    // The words for tacky's vocabulary. Kept here rather than in the model: the
    // model speaks XMPP's terms, and this is the only place they are read aloud.
    function groupLabel(group) {
        switch (group) {
        case "moderator":   return qsTr("Moderators")
        case "participant": return qsTr("Participants")
        case "visitor":     return qsTr("Visitors")
        default:            return qsTr("Others")
        }
    }
    function roleLabel(role) {
        switch (role) {
        case "moderator":   return qsTr("Moderator")
        case "participant": return qsTr("Participant")
        case "visitor":     return qsTr("Visitor")
        default:            return qsTr("No role")
        }
    }
    function affiliationLabel(affiliation) {
        switch (affiliation) {
        case "owner":   return qsTr("Owner")
        case "admin":   return qsTr("Admin")
        case "member":  return qsTr("Member")
        case "outcast": return qsTr("Banned")
        default:        return ""
        }
    }
    function showLabel(show) {
        switch (show) {
        case "away": return qsTr("Away")
        case "xa":   return qsTr("Away for a while")
        case "dnd":  return qsTr("Do not disturb")
        default:     return qsTr("Available")
        }
    }
    function showColor(show) {
        switch (show) {
        case "away":
        case "xa":   return Theme.warning
        case "dnd":  return Theme.negative
        default:     return Theme.positive
        }
    }
    // Extended away is away's colour drawn hollow, so the two read as one idea
    // rather than one of them borrowing a third state's colour.
    function showFilled(show) { return show !== "xa" }

    function copyText(text) {
        Clipboard.setText(text)
        copiedNotice.show()
    }

    property bool filterMode: false
    function openFilter() {
        page.filterMode = true
        filterInput.forceActiveFocus()
        // A filter opened halfway down a room would otherwise start on rows
        // the typing is about to throw away, at a scroll offset the departing
        // cards have just moved out from under it.
        list.positionViewAtBeginning()
    }
    // Answers whether it had anything to close, so an Android back press knows
    // whether it was spent here.
    function closeFilter() {
        if (!page.filterMode)
            return false
        page.filterMode = false
        filterInput.text = ""
        return true
    }

    // The pieces below are self-contained on purpose: an inline component is
    // its own scope and cannot read this file's ids.

    // A word about a row, drawn as a soft pill in whatever colour it is handed.
    component Chip: Rectangle {
        id: chip
        property string text: ""
        property color tone: Theme.textDim
        implicitWidth: chipText.implicitWidth + 16
        implicitHeight: 22
        radius: 11
        color: Qt.rgba(chip.tone.r, chip.tone.g, chip.tone.b, 0.15)
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            color: chip.tone
            font.pixelSize: 11
            font.bold: true
        }
    }

    // The presence mark on an occupant's avatar. The ring is the colour of the
    // row behind it, so the dot reads as punched out of the row rather than
    // dropped on top of the picture.
    component PresenceDot: Rectangle {
        id: dot
        property color tone: Theme.positive
        property bool filled: true
        implicitWidth: 14
        implicitHeight: 14
        radius: width / 2
        color: Theme.background
        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 10
            radius: width / 2
            antialiasing: true
            color: dot.filled ? dot.tone : "transparent"
            border.width: dot.filled ? 0 : 2
            border.color: dot.tone
        }
    }

    // One line of the legend: a word, and what it means.
    component LegendRow: RowLayout {
        id: legendRow
        property string term: ""
        property string meaning: ""
        Layout.fillWidth: true
        spacing: 10
        Text {
            Layout.preferredWidth: 104
            Layout.alignment: Qt.AlignTop
            text: legendRow.term
            color: Theme.textPrimary
            font.pixelSize: 12
            font.bold: true
        }
        Text {
            Layout.fillWidth: true
            text: legendRow.meaning
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    component LegendHeading: Text {
        Layout.fillWidth: true
        Layout.topMargin: 6
        color: Theme.textDim
        font.pixelSize: 11
        font.bold: true
    }

    header: PageHeader {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: page.showClose ? 4 : 16
            anchors.rightMargin: 8
            spacing: 4
            visible: !page.filterMode

            IconButton {
                iconPath: Icons.arrowBack
                Accessible.name: qsTr("Back")
                visible: page.showClose
                glyphColor: Theme.textDim
                onClicked: page.done()
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Room details")
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    objectName: "detailsSubtitle"
                    Layout.fillWidth: true
                    text: page.roomTitle
                    color: Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
            IconButton {
                objectName: "filterButton"
                iconPath: Icons.search
                iconSize: 20
                Accessible.name: qsTr("Find someone")
                glyphColor: Theme.textDim
                onClicked: page.openFilter()
            }
            IconButton {
                objectName: "roomMenuButton"
                iconPath: Icons.moreHoriz
                iconSize: 20
                Accessible.name: qsTr("Room actions")
                glyphColor: Theme.textDim
                onClicked: roomMenu.popup()
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 14
            spacing: 6
            visible: page.filterMode

            IconButton {
                iconPath: Icons.close
                Accessible.name: qsTr("Clear filter")
                onClicked: page.closeFilter()
            }
            TextField {
                id: filterInput
                objectName: "occupantFilter"
                Layout.fillWidth: true
                placeholderText: qsTr("Find someone in this room")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                Keys.onEscapePressed: page.closeFilter()
            }
            Text {
                objectName: "filterCount"
                //: How many of the room's people the filter left showing
                text: qsTr("%1 of %2").arg(room.count).arg(room.total)
                color: Theme.textDim
                font.pixelSize: 12
            }
        }
    }

    // Room verbs. Whether the room will accept one is between it and you: only
    // the two the Tk GUI gates - asking for a voice you already have, and
    // destroying a room you do not own - are hidden rather than refused.
    AppMenu {
        id: roomMenu
        objectName: "roomMenu"
        width: 240

        MenuEntry {
            objectName: "inviteEntry"
            text: qsTr("Invite someone…")
            onTriggered: page.ask("invite", qsTr("Invite to %1").arg(page.roomTitle),
                                  qsTr("Their address:"))
        }
        MenuEntry {
            objectName: "subjectEntry"
            text: qsTr("Change subject…")
            onTriggered: page.ask("subject", qsTr("Room subject"),
                                  qsTr("What this room is about. Most rooms only "
                                       + "let their moderators set it."),
                                  room.subject)
        }
        MenuEntry {
            objectName: "nickEntry"
            text: qsTr("Change my nickname…")
            onTriggered: page.ask("nick", qsTr("Your nickname"),
                                  qsTr("How this room sees you. Kept in the "
                                       + "bookmark, so the next join uses it too."),
                                  room.myNick)
        }
        MenuEntry {
            objectName: "requestVoiceEntry"
            text: qsTr("Request voice")
            offered: room.myRole === "visitor"
            onTriggered: room.requestVoice()
        }
        MenuEntry {
            objectName: "destroyEntry"
            text: qsTr("Destroy room…")
            labelColor: Theme.negative
            offered: room.myAffiliation === "owner"
            onTriggered: {
                destroyConfirm.message =
                    qsTr("Destroy %1 for everyone, permanently?").arg(room.roomJid)
                destroyConfirm.open()
            }
        }
    }

    // Put one of the questions below, filling in the dialog first. `about` is
    // whoever a moderation reason is about, and rides along until the answer
    // comes back.
    function ask(purpose, title, question, value, about) {
        prompt.purpose = purpose
        prompt.title = title
        prompt.prompt = question
        prompt.value = value ?? ""
        prompt.subject = about ?? ""
        prompt.open()
    }

    // One dialog for every question this page asks, which is what
    // TextPromptDialog's `subject` is for; `purpose` says which was asked.
    TextPromptDialog {
        id: prompt
        objectName: "roomPrompt"
        property string purpose: ""
        onSubmitted: (text) => {
            switch (prompt.purpose) {
            case "invite":
                if (text !== "")
                    room.invite(Jid.bare(text))
                break
            case "subject":
                room.setSubject(text)
                break
            case "nick":
                if (text !== "")
                    room.changeNick(text)
                break
            // A blank reason means none was given rather than "cancelled",
            // which is why these two go ahead on an empty answer.
            case "kick":
                room.kick(prompt.subject, text)
                break
            case "ban":
                room.setAffiliation(prompt.subject, "outcast", text)
                break
            }
        }
    }

    ConfirmDialog {
        id: destroyConfirm
        objectName: "destroyConfirm"
        title: qsTr("Destroy room")
        onAccepted: room.destroyRoom()
    }

    // Every moderation action can be refused, and saying so is the difference
    // between the room turning it down and a button that did nothing.
    SheetDialog {
        id: actionError
        objectName: "actionErrorDialog"
        property alias message: actionErrorText.text
        standardButtons: Dialog.Ok
        Text {
            id: actionErrorText
            objectName: "actionErrorMessage"
            width: actionError.availableWidth
            color: Theme.textPrimary
            wrapMode: Text.WordWrap
        }
    }

    // What may be done to one occupant, straight off the caps tacky stamped the
    // row with. Loaded from the row before it pops up, the way the chat list's
    // menu is: a menu per delegate is a windowful of scenery for a row that is
    // usually only looked at.
    AppMenu {
        id: occupantMenu
        objectName: "occupantMenu"
        width: 240

        property string nick: ""
        property string realJid: ""
        property var caps: ({})

        function openFor(row) {
            occupantMenu.nick = row.nick
            occupantMenu.realJid = row.realJid
            occupantMenu.caps = row.caps
            occupantMenu.popup()
        }
        function can(what) { return occupantMenu.caps[what] === true }

        MenuEntry {
            objectName: "kickEntry"
            text: qsTr("Kick…")
            offered: occupantMenu.can("kick")
            onTriggered: page.ask("kick", qsTr("Kick %1").arg(occupantMenu.nick),
                                  qsTr("Reason (optional). A kick only lasts "
                                       + "until they walk back in."),
                                  "", occupantMenu.nick)
        }
        MenuEntry {
            objectName: "banEntry"
            text: qsTr("Ban…")
            labelColor: Theme.negative
            offered: occupantMenu.can("ban")
            // Against the real JID: an affiliation outlives the nick it was set
            // on, which is what makes a ban a ban and a kick only a kick.
            onTriggered: page.ask("ban", qsTr("Ban %1").arg(occupantMenu.nick),
                                  qsTr("Reason (optional). A ban keeps them out "
                                       + "until it is lifted."),
                                  "", occupantMenu.realJid)
        }
        MenuEntry {
            objectName: "makeModeratorEntry"
            text: qsTr("Make moderator")
            offered: occupantMenu.can("make_moderator")
            onTriggered: room.setRole(occupantMenu.nick, "moderator")
        }
        MenuEntry {
            objectName: "grantVoiceEntry"
            text: qsTr("Grant voice")
            offered: occupantMenu.can("grant_voice")
            onTriggered: room.setRole(occupantMenu.nick, "participant")
        }
        MenuEntry {
            objectName: "revokeVoiceEntry"
            text: qsTr("Revoke voice")
            offered: occupantMenu.can("revoke_voice")
            onTriggered: room.setRole(occupantMenu.nick, "visitor")
        }
        MenuEntry {
            objectName: "grantMembershipEntry"
            text: qsTr("Grant membership")
            offered: occupantMenu.can("grant_membership")
            onTriggered: room.setAffiliation(occupantMenu.realJid, "member")
        }
        MenuEntry {
            objectName: "revokeMembershipEntry"
            text: qsTr("Revoke membership")
            offered: occupantMenu.can("revoke_membership")
            onTriggered: room.setAffiliation(occupantMenu.realJid, "none")
        }
        MenuEntry {
            objectName: "copyOccupantJidEntry"
            text: qsTr("Copy address")
            offered: occupantMenu.realJid !== ""
            onTriggered: page.copyText(occupantMenu.realJid)
        }
    }

    ListView {
        id: list
        objectName: "occupantList"
        anchors.fill: parent
        clip: true
        model: room
        ScrollBar.vertical: ThinScrollBar {}

        // Inline rather than pinned to the top of the view: a pinned label would
        // float over the room card while the list is still at the top.
        section.property: "group"
        section.criteria: ViewSection.FullString

        section.delegate: Rectangle {
            id: groupHeading
            objectName: "groupHeading"
            required property string section
            width: list.width
            height: 30
            color: Theme.background

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: page.groupLabel(groupHeading.section) + "  ·  "
                      + (room.groupCounts[groupHeading.section] ?? 0)
                color: Theme.textDim
                font.pixelSize: 12
                font.bold: true
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.hairline
            }
        }

        header: Item {
            width: list.width
            implicitHeight: headerColumn.implicitHeight + 26

            ColumnLayout {
                id: headerColumn
                width: parent.width - 32
                x: 16
                y: 16
                spacing: 14

                // Who the room is, above anything about who is in it. Both
                // cards stand down while the filter is up: they are about the
                // room, and what is being asked for is a person - a screenful
                // of subject would only push the matches off the top.
                Card {
                    objectName: "roomCard"
                    visible: !page.filterMode

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        Avatar {
                            Layout.preferredWidth: 60
                            Layout.preferredHeight: 60
                            Layout.alignment: Qt.AlignTop
                            account: page.account
                            jid: page.jid
                            label: page.roomTitle
                            initialsPixelSize: 24
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                objectName: "roomName"
                                Layout.fillWidth: true
                                text: page.roomTitle
                                color: Theme.textPrimary
                                font.pixelSize: 19
                                font.bold: true
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: room.roomJid
                                    color: Theme.textDim
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                IconButton {
                                    objectName: "copyRoomJid"
                                    iconPath: Icons.contentCopy
                                    iconSize: 15
                                    Accessible.name: qsTr("Copy room address")
                                    glyphColor: Theme.textDim
                                    implicitWidth: 30
                                    implicitHeight: 30
                                    onClicked: page.copyText(room.roomJid)
                                }
                            }
                            RowLayout {
                                Layout.topMargin: 2
                                spacing: 6
                                Chip {
                                    objectName: "occupantCountChip"
                                    text: qsTr("%n person(s)", "", room.total)
                                    tone: Theme.accentDeep
                                }
                                Chip {
                                    objectName: "notJoinedChip"
                                    visible: !room.joined
                                    text: qsTr("Not joined")
                                    tone: Theme.warning
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        Layout.topMargin: 2
                        color: Theme.hairline
                    }

                    // The subject: the one thing about a room that can change
                    // while you are reading it.
                    Text {
                        objectName: "roomSubject"
                        Layout.fillWidth: true
                        visible: room.subject !== ""
                        text: room.subject
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                    Caption {
                        objectName: "noSubjectNotice"
                        Layout.fillWidth: true
                        visible: room.subject === ""
                        text: room.joined
                              ? qsTr("No subject set.")
                              : qsTr("Nothing is known about this room until you are in it.")
                        wrapMode: Text.WordWrap
                    }
                }

                // Where you stand in there, which decides half of what the menu
                // above offers.
                Card {
                    objectName: "youCard"
                    visible: !page.filterMode && room.myNick !== ""

                    Text {
                        text: qsTr("You in this room")
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        font.bold: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        // Through the model, which refuses to hand over half a
                        // JID. This card is hidden until the nick lands, but a
                        // hidden item's bindings run all the same, and asking
                        // tacky about "/" is an error rather than a miss.
                        Avatar {
                            objectName: "myAvatar"
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            account: page.account
                            jid: room.myOccupantJid
                            label: room.myNick
                            initialsPixelSize: 14
                        }
                        Text {
                            objectName: "myNick"
                            Layout.fillWidth: true
                            text: room.myNick
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Chip {
                            objectName: "myRoleChip"
                            text: page.roleLabel(room.myRole)
                            tone: Theme.textDim
                        }
                        Chip {
                            objectName: "myAffiliationChip"
                            visible: page.affiliationLabel(room.myAffiliation) !== ""
                            text: page.affiliationLabel(room.myAffiliation)
                            tone: Theme.accentDeep
                        }
                    }
                    Caption {
                        Layout.fillWidth: true
                        visible: room.myRole === "visitor"
                        text: qsTr("Visitors cannot speak here. Ask for voice from the room menu.")
                        wrapMode: Text.WordWrap
                    }
                }

                SectionLabel {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    text: room.filter === ""
                        ? qsTr("People")
                        : qsTr("People matching “%1”").arg(room.filter)
                    shown: room.total > 0
                }
            }
        }

        delegate: ItemDelegate {
            id: occupantRow
            required property string nick
            required property string occupantJid
            required property string realJid
            required property string role
            required property string affiliation
            required property string show
            required property string status
            required property var caps
            required property bool self

            width: ListView.view.width
            height: 58
            onClicked: if (occupantRow.canModerate) occupantMenu.openFor(occupantRow)

            // Nothing to offer means nothing to press. The row is still a row -
            // it just does not pretend to be a button.
            readonly property bool canModerate: {
                for (const key in occupantRow.caps)
                    if (occupantRow.caps[key] === true)
                        return true
                return false
            }

            // Under the nick: who they really are where the room says so, and
            // otherwise whatever they said about themselves, quoted because it
            // is theirs and not ours. Deliberately not a presence word to fall
            // back on - the dot already carries that, and a row reading
            // "Available" beside one reading "online" makes the app's own label
            // look like somebody's status text.
            readonly property string secondLine: {
                if (occupantRow.realJid !== "")
                    return occupantRow.realJid
                return occupantRow.status !== ""
                    ? qsTr("“%1”").arg(occupantRow.status) : ""
            }

            background: Rectangle {
                color: occupantRow.hovered && occupantRow.canModerate
                       ? Theme.menuHover : "transparent"
            }

            contentItem: RowLayout {
                spacing: 12

                Item {
                    Layout.leftMargin: 16
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    Avatar {
                        anchors.fill: parent
                        account: page.account
                        jid: occupantRow.occupantJid
                        label: occupantRow.nick
                        initialsPixelSize: 15
                    }
                    PresenceDot {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        tone: page.showColor(occupantRow.show)
                        filled: page.showFilled(occupantRow.show)
                        Accessible.name: page.showLabel(occupantRow.show)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            objectName: "occupantNick"
                            Layout.fillWidth: true
                            text: occupantRow.nick
                            color: Theme.textPrimary
                            font.pixelSize: 15
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        // The affiliation prefixes the Tk list puts in front of
                        // a nick, as something readable without a key.
                        Chip {
                            objectName: "affiliationChip"
                            visible: page.affiliationLabel(occupantRow.affiliation) !== ""
                            text: page.affiliationLabel(occupantRow.affiliation)
                            tone: occupantRow.affiliation === "owner"
                                  ? Theme.accentDeep : Theme.textDim
                        }
                        Chip {
                            objectName: "selfChip"
                            visible: occupantRow.self
                            text: qsTr("You")
                            tone: Theme.accent
                        }
                    }
                    Text {
                        objectName: "occupantSecondLine"
                        Layout.fillWidth: true
                        // Nothing known about them beyond the nick, so the nick
                        // takes the whole row rather than sitting over a gap.
                        visible: occupantRow.secondLine !== ""
                        text: occupantRow.secondLine
                        color: Theme.textDim
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }

                IconButton {
                    objectName: "occupantMenuButton"
                    Layout.rightMargin: 8
                    iconPath: Icons.moreHoriz
                    iconSize: 18
                    Accessible.name: qsTr("Actions for this person")
                    glyphColor: Theme.textDim
                    visible: occupantRow.canModerate
                    onClicked: occupantMenu.openFor(occupantRow)
                }
            }
        }

        footer: Item {
            width: list.width
            implicitHeight: footerColumn.implicitHeight + 34

            ColumnLayout {
                id: footerColumn
                width: parent.width - 32
                x: 16
                y: 16
                spacing: 14

                // An empty list has three different reasons, and should say
                // which one it is.
                Caption {
                    objectName: "emptyNotice"
                    Layout.fillWidth: true
                    visible: room.count === 0
                    wrapMode: Text.WordWrap
                    text: {
                        if (!room.joined)
                            return qsTr("You are not in this room, so nobody is listed. Join it from the chat list to see who is here.")
                        if (room.total === 0)
                            return qsTr("Nobody is here yet.")
                        return qsTr("Nobody in this room matches that.")
                    }
                }

                // What participantlegend.tcl is a whole toplevel for, folded in
                // beside the words it explains - and folded away, because it is
                // read once and then never again.
                Card {
                    id: legendCard
                    objectName: "legendCard"
                    property bool open: false

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("What do these mean?")
                            color: Theme.textPrimary
                            font.pixelSize: 14
                            font.bold: true
                        }
                        Glyph {
                            path: legendCard.open ? Icons.keyboardArrowUp
                                                  : Icons.keyboardArrowDown
                            color: Theme.textDim
                            size: 20
                        }
                        TapHandler {
                            objectName: "legendToggle"
                            onTapped: legendCard.open = !legendCard.open
                        }
                    }

                    ColumnLayout {
                        objectName: "legendBody"
                        Layout.fillWidth: true
                        visible: legendCard.open
                        spacing: 4

                        LegendHeading { text: qsTr("Roles, granted for this visit") }
                        LegendRow {
                            term: qsTr("Moderator")
                            meaning: qsTr("Can kick, mute and manage the room")
                        }
                        LegendRow {
                            term: qsTr("Participant")
                            meaning: qsTr("Can speak")
                        }
                        LegendRow {
                            term: qsTr("Visitor")
                            meaning: qsTr("Can read, but not speak in a moderated room")
                        }

                        LegendHeading { text: qsTr("Affiliations, kept between visits") }
                        LegendRow {
                            term: qsTr("Owner")
                            meaning: qsTr("Full control, down to destroying the room")
                        }
                        LegendRow {
                            term: qsTr("Admin")
                            meaning: qsTr("Can ban people and grant membership")
                        }
                        LegendRow {
                            term: qsTr("Member")
                            meaning: qsTr("Recognised by the room, and let into a members-only one")
                        }

                        LegendHeading { text: qsTr("The dot on a picture") }
                        LegendRow {
                            term: qsTr("Filled green")
                            meaning: qsTr("Available")
                        }
                        LegendRow {
                            term: qsTr("Filled amber")
                            meaning: qsTr("Away")
                        }
                        LegendRow {
                            term: qsTr("Hollow amber")
                            meaning: qsTr("Away for a while")
                        }
                        LegendRow {
                            term: qsTr("Filled red")
                            meaning: qsTr("Does not want to be disturbed")
                        }
                    }
                }
            }
        }
    }

    CopiedNotice {
        id: copiedNotice
        objectName: "copiedNotice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        text: qsTr("Address copied")
    }
}
