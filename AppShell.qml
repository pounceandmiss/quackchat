pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quack

// Wide window: list | chat side by side. Narrow: one column showing the list,
// or the open chat pushed over it with a back button. The account rail is a
// pull-out drawer at either width, reached from the list's header. One instance
// per window.
Item {
    id: shell
    objectName: "appShell"
    property string initialAccount: ""
    property string currentAccount: initialAccount
    property string currentChatJid: ""
    property string currentChatName: ""
    property bool currentChatGroupchat: false

    readonly property bool wide: width >= 720

    // `listWidth` is what the divider was last dragged to, `listSpan` what the
    // shell can actually give it. Keeping them apart lets a window too narrow to
    // honour the drag squeeze the column without forgetting what it was asked
    // for.
    property real listWidth: 320
    readonly property real listSpan: wide ? clampListWidth(listWidth) : width
    // Narrower than this stops being a conversation list; wider than half the
    // window takes the larger share from the chat.
    function clampListWidth(w) {
        return Math.max(220, Math.min(w, width * 0.5))
    }

    // Fall back to the first available account when none is selected (or the
    // selected one disappears, e.g. after a remove).
    function ensureAccount() {
        if (currentAccount === "" || !App.accounts.contains(currentAccount))
            currentAccount = App.accounts.firstJid()
    }
    Component.onCompleted: ensureAccount()
    Connections {
        target: App.accounts
        function onCountChanged() { shell.ensureAccount() }
    }

    // The account-wide search stands in for the conversations list rather than
    // opening beside it, so it works the same in the wide and stacked layouts.
    // Searching inside one chat happens in the chat's own header.
    property bool searching: false

    // Stacked, opening a chat is a push: it arrives from the right over the
    // conversations list, which drifts the other way underneath it. While it
    // runs both panes are on screen at full width, which is why they are placed
    // by hand rather than by a SplitView: no two panes of a split can overlap.
    //
    // Each pane is moved by an Animator of its own rather than by a Behavior on
    // one shared 0..1 progress: Animators run on the render thread, so the push
    // keeps its frames while the GUI thread lays out the arriving chat. The
    // price is that an animated property reads its old value until it lands, so
    // nothing may be bound to a pane's position while it moves.
    readonly property bool chatOnTop: !wide && currentChatJid !== "" && !popping
    // Up while a push is in flight. Started from the state change rather than
    // from openChat and closeChat, so a chat opened by writing the property
    // pushes like any other.
    property bool sliding: false
    onChatOnTopChanged: if (!wide && easeSlide) {
        sliding = true
        pushSettle.restart()
    }
    // A pane is up if the push is heading its way or it has not left yet.
    readonly property bool listUp: !chatOnTop || sliding
    readonly property bool chatUp: chatOnTop || sliding

    readonly property int pushDuration: 250
    // Fast-out-slow-in, not a plain Out*: those leave at full speed and spend
    // the back half of the duration crawling the last few pixels, which reads
    // as the push stalling in mid-air.
    readonly property list<real> pushCurve: [0.4, 0.0, 0.2, 1.0, 1.0, 1.0]

    // Off for what is not navigation: switching account discards the open chat
    // rather than popping it, and an empty pane leaving is nothing to watch.
    property bool easeSlide: true

    // What ends a push: the animators are on the render thread and report
    // nothing back. Late by a margin on purpose - early would take the list out
    // from under a chat that has not quite landed.
    Timer {
        id: pushSettle
        interval: shell.pushDuration + 50
        onTriggered: {
            shell.sliding = false
            // Clear before dropping `popping`, or there is an instant with a
            // chat open and nothing popping: a push back to what has just left.
            if (shell.popping) {
                shell.clearChat()
                shell.popping = false
            }
        }
    }

    // The chat has to stay on screen for as long as it takes to slide off, so
    // a pop lets go of it at the end of the slide rather than the start.
    property bool popping: false

    function clearChat() {
        currentChatJid = ""
        currentChatName = ""
        currentChatGroupchat = false
    }

    // openChat by JID alone, for callers that have no row in hand - a desktop
    // notification carries the sender's nick, which in a room is the speaker
    // rather than the chat. Also switches account, since the alert may well be
    // for one this window is not showing.
    function showChat(account, jid) {
        const list = App.chatListFor(account)
        const entry = list ? list.entryFor(jid) : ({})
        currentAccount = account
        searching = false
        openChat(jid, entry.name || jid, entry.groupchat === true)
    }

    // Which chat and what kind of chat are two properties, and the session
    // binding is re-read between the two writes. Start from no chat open, so no
    // half-written pair names one.
    function openChat(jid, name, groupchat) {
        popping = false // a pop still running is overtaken, not queued behind
        currentChatJid = ""
        currentChatGroupchat = groupchat
        currentChatName = name
        currentChatJid = jid
    }

    function closeChat() {
        if (!wide && currentChatJid !== "") {
            popping = true
            return
        }
        clearChat()
    }

    function openSearch() {
        searching = true
        searchPane.focusInput()
    }

    // Where Ctrl+F lands: an open conversation is what you are most likely
    // looking through, and the whole account is what you mean when there is
    // none.
    function startFind() {
        if (currentChatJid !== "")
            chatPage.openSearch()
        else
            openSearch()
    }

    onCurrentAccountChanged: {
        // Not a navigation: the open chat belongs to the account just left, so
        // it goes at once rather than sliding off as somebody else's pane.
        // easeSlide goes first: dropping `popping` with the Behavior still on
        // starts a slide back to the chat that the clear below cannot call off.
        easeSlide = false
        pushSettle.stop()
        sliding = false
        popping = false
        clearChat()
        easeSlide = true
        searching = false
    }

    // Android's system back arrives as a close request. Unwind one navigation
    // step instead: message selection first, then the open chat in the
    // stacked layout. Returns false when there's nothing left to pop.
    function handleBack() {
        // Covers the shell while open, so it unwinds before anything under it.
        if (railDrawer.opened) {
            railDrawer.close()
            return true
        }
        if (chatPage.closeXml())
            return true
        if (chatPage.closeKeys())
            return true
        if (chatPage.closeSearch())
            return true
        if (chatPage.selectionMode) {
            chatPage.clearSelection()
            return true
        }
        // Search sits on top of the chat in the stacked layout, so it unwinds
        // first - the order things were opened in.
        if (searching) {
            searching = false
            return true
        }
        if (!wide && currentChatJid !== "") {
            closeChat()
            return true
        }
        return false
    }

    ConversationsPage {
        // Stacked, drifts left under the arriving chat rather than sitting
        // still, so the two do not read as one sheet.
        x: shell.chatOnTop ? -shell.width * 0.22 : 0
        width: shell.listSpan
        height: shell.height
        // Hidden once a chat is open in the narrow layout - but not before the
        // push lands, or there would be no list for the chat to slide over.
        visible: !shell.searching && (shell.wide || shell.listUp)
        Behavior on x {
            enabled: !shell.wide && shell.easeSlide
            XAnimator {
                duration: shell.pushDuration
                easing.type: Easing.Bezier
                easing.bezierCurve: shell.pushCurve
            }
        }
        account: shell.currentAccount
        onOpenAccounts: railDrawer.open()
        onOpenChat: (jid, name, groupchat) => shell.openChat(jid, name, groupchat)
        onPopOutChat: (jid, name, groupchat) => {
            if (!Theme.mobile)
                AppWindows.popOut(shell.currentAccount, jid, name, groupchat)
        }
        onStartSearch: shell.openSearch()
    }

    SearchPage {
        id: searchPane
        // Stacked, it takes the whole width from the open chat; side by side it
        // takes the conversations list's column.
        width: shell.listSpan
        height: shell.height
        visible: shell.searching
        account: shell.currentAccount
        onClosed: shell.searching = false
        // The hit names its own chat, which need not be the open one. Whether
        // that chat is a room is the chat list's answer, not a reading of the
        // JID.
        onOpenHit: (jid, ts, matches) => {
            if (jid !== shell.currentChatJid) {
                const list = App.chatListFor(shell.currentAccount)
                const entry = list ? list.entryFor(jid) : ({})
                shell.openChat(jid, entry.name ?? "", entry.groupchat === true)
            }
            chatPage.jumpTo(ts, matches)
            // Stacked, the results are covering the message they point at; side
            // by side both are on screen and the list stays.
            if (!shell.wide)
                shell.searching = false
        }
    }

    // Dim over the list for as long as the push is in flight, so it reads as a
    // layer lifted over the list rather than the two trading places. Declared
    // before the chat, so it lands between it and the list.
    Rectangle {
        anchors.fill: parent
        visible: !shell.wide && shell.sliding
        color: "#000000"
        opacity: shell.chatOnTop ? 0.25 : 0
        Behavior on opacity {
            enabled: !shell.wide && shell.easeSlide
            OpacityAnimator {
                duration: shell.pushDuration
                easing.type: Easing.Bezier
                easing.bezierCurve: shell.pushCurve
            }
        }
    }

    // Wide, the chat is the column past the divider, taking whatever the list
    // leaves. Stacked, it is a card pushed over the list, so it takes the full
    // width and rides in from the right edge.
    ChatPage {
        id: chatPage
        x: shell.wide ? shell.listSpan + 1 : (shell.chatOnTop ? 0 : shell.width)
        // From the column, not from this pane's own x, which lags the animator.
        width: shell.wide ? shell.width - shell.listSpan - 1 : shell.width
        height: shell.height
        visible: shell.wide || (shell.chatUp && !shell.searching)
        Behavior on x {
            enabled: !shell.wide && shell.easeSlide
            XAnimator {
                duration: shell.pushDuration
                easing.type: Easing.Bezier
                easing.bezierCurve: shell.pushCurve
            }
        }
        account: shell.currentAccount
        chatJid: shell.currentChatJid
        chatName: shell.currentChatName
        chatGroupchat: shell.currentChatGroupchat
        showBack: !shell.wide
        canPopOut: shell.wide && !Theme.mobile
        onBack: shell.closeChat()
        onPopOut: {
            AppWindows.popOut(shell.currentAccount, shell.currentChatJid,
                              shell.currentChatName,
                              shell.currentChatGroupchat)
            shell.closeChat()
        }
    }

    // The soft edge at the chat's leading side. A child of the chat so that it
    // travels with it: bound to its x it would sit still until the push landed.
    Rectangle {
        parent: chatPage
        x: -width
        width: 18
        height: chatPage.height
        visible: !shell.wide && shell.sliding
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#38000000" }
        }
    }

    // The seam between the two columns, and the strip that drags it. Declared
    // after both, so a press on the hairline moves the divider rather than
    // landing in the chat behind it.
    Item {
        id: divider
        x: shell.listSpan
        // Reaches to the right of the hairline only, so it is grabbable by touch
        // without covering the list's scrollbar.
        width: 18
        height: shell.height
        visible: shell.wide

        readonly property bool engaged: dividerHover.hovered || dividerDrag.active

        HoverHandler {
            id: dividerHover
            cursorShape: Qt.SplitHCursor
        }
        DragHandler {
            id: dividerDrag
            target: null
            yAxis.enabled: false
            // Where the column stood when the drag began; the handler's
            // translation is measured from there.
            property real grabbedSpan: 0
            onActiveChanged: if (active) grabbedSpan = shell.listSpan
        }
        // The dragged width is a function of where the drag started and how far
        // it has come, so it is written as one. RestoreNone: where it lands is
        // the new width, not something to undo on release.
        Binding {
            target: shell
            property: "listWidth"
            when: dividerDrag.active
            restoreMode: Binding.RestoreNone
            value: shell.clampListWidth(dividerDrag.grabbedSpan
                                        + dividerDrag.activeTranslation.x)
        }

        Rectangle {
            width: 1
            height: parent.height
            color: Theme.hairline
        }
        // Always-on grab nub, for touch discoverability. Centred on the
        // hairline, not in the strip: the strip is all to one side of it.
        Rectangle {
            anchors.horizontalCenter: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 4
            height: 36
            radius: 2
            color: Theme.textDim
            opacity: divider.engaged ? 0 : 0.35
        }
        // Accent highlight along the divider while dragging.
        Rectangle {
            anchors.horizontalCenter: parent.left
            width: 3
            height: parent.height
            color: Theme.accent
            opacity: divider.engaged ? (dividerDrag.active ? 0.8 : 0.45) : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
    }

    // A Drawer parents to the window overlay, so it covers the whole window
    // rather than the shell's inset area; the rail reads the safe margins back
    // for itself.
    Drawer {
        id: railDrawer
        objectName: "accountDrawer"
        edge: Qt.LeftEdge
        // Leaves a strip of the list showing, so it reads as a layer over the
        // shell rather than a screen navigated to; capped for wider windows.
        width: Math.min(shell.width - 56, 320)
        // A Drawer sizes to its content, and this rail is built from anchors, so
        // it reports no implicit height: without this it opens zero-height, with
        // the ＋ button spilling out and nothing else drawn. `parent` is the
        // window overlay.
        height: parent ? parent.height : 0
        padding: 0
        // Over a chat pushed on top of the list, the left edge belongs to going
        // back rather than to the rail.
        interactive: !shell.chatOnTop
        background: Rectangle {
            // Matches the rail filling it, so the slide shows no seam.
            color: Theme.rail
            Rectangle {
                anchors.right: parent.right
                width: 1; height: parent.height
                color: Theme.hairline
            }
        }

        AccountRail {
            anchors.fill: parent
            currentAccount: shell.currentAccount
            // It covers the list the pick was made for, so it closes behind you.
            onSelectAccount: (jid) => {
                shell.currentAccount = jid
                railDrawer.close()
            }
        }
    }
}
