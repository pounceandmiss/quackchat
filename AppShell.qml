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

    // Stacked, opening a chat is a push: it arrives from the right over the
    // conversations list, which drifts the other way underneath it. While it
    // runs both panes are on screen at full width, which is why they are placed
    // by hand rather than by a SplitView: no two panes of a split can overlap.
    //
    // `slide` is how far the push has got - 0 at the list, 1 at the chat - and
    // it is the only thing that moves: both panes are bound to it and neither
    // is animated in its own right. That is what lets a finger take a push over
    // half way through and turn it round, which is worth more here than running
    // the push on the render thread would be: an Animator reads its old value
    // until it lands, so a gesture has no position to carry on from.
    readonly property bool chatOnTop: !wide && currentChatJid !== "" && !popping
    property real slide: 0
    readonly property bool sliding: slide > 0 && slide < 1
    // A pane is up if the push is heading its way or it has not left yet. The
    // finger counts as in flight from the moment it takes hold, so the list is
    // already there for the first pixel of the swipe to uncover.
    readonly property bool listUp: !chatOnTop || slide < 1 || backDrag.active
    readonly property bool chatUp: chatOnTop || slide > 0

    // What the whole width costs a push nobody is driving; a shorter trip costs
    // proportionally less.
    readonly property int pushDuration: 250
    // Fast-out-slow-in, not a plain Out*: those leave at full speed and spend
    // the back half of the duration crawling the last few pixels, which reads
    // as the push stalling in mid-air.
    readonly property list<real> pushCurve: [0.4, 0.0, 0.2, 1.0, 1.0, 1.0]

    // Off for what is not navigation: switching account discards the open chat
    // rather than popping it, so there is nothing there to watch slide off.
    property bool easeSlide: true

    // Two animations rather than one with its curve swapped: a push nobody is
    // holding should leave gently, while one handed over by a moving finger has
    // to leave at the speed that finger had, or the handover reads as a stall.
    NumberAnimation {
        id: pushEase
        target: shell
        property: "slide"
        easing.type: Easing.Bezier
        easing.bezierCurve: shell.pushCurve
    }
    NumberAnimation {
        id: throwEase
        target: shell
        property: "slide"
        easing.type: Easing.OutQuad
    }

    // Started from the state change rather than from openChat and closeChat, so
    // a chat opened by writing the property pushes like any other.
    onChatOnTopChanged: settleTo(chatOnTop ? 1 : 0, 0)

    // Carry the push to `to` from wherever it stands. `speed` is how fast a
    // finger let go of it, in pixels a second, and 0 for a push nobody threw.
    function settleTo(to, speed) {
        // The finger is the animation for as long as it lasts; whatever moved
        // under it is settled on release.
        if (backDrag.active)
            return
        holdSlide()
        const px = Math.abs(to - slide) * width
        if (wide || !easeSlide || px < 1) {
            slide = to
            // Nothing left to animate, so the pop has to be let go of here: a
            // swipe carried the whole way is already home when the finger lifts.
            if (to === 0)
                endPop()
            return
        }
        if (speed <= 0) {
            pushEase.to = to
            pushEase.duration = Math.max(90, Math.round(pushDuration * px / width))
            pushEase.start()
            return
        }
        // An ease-out leaves at twice its average speed, so picking up from a
        // moving finger without a jerk takes twice the time that finger would
        // have needed for what is left. A throw only ever shortens the trip.
        throwEase.to = to
        throwEase.duration = Math.max(90, Math.min(Math.round(pushDuration * px / width),
                                                   Math.round(2000 * px / speed)))
        throwEase.start()
    }

    // Take the slide off whatever was carrying it and leave it where it stands
    // - a position at all only because `slide` is what is animated, so stopping
    // it short says where the pane is rather than where it was headed.
    function holdSlide() {
        pushEase.stop()
        throwEase.stop()
    }

    // The chat has to stay on screen for as long as it takes to slide off, so
    // a pop lets go of it at the end of the slide rather than the start.
    property bool popping: false
    onSlideChanged: if (slide === 0) endPop()
    // Queued rather than run where it is called from: it clears the chat that
    // `chatOnTop` is read from, and a swipe carried the whole way gets here
    // from inside that property's own change handler, where writing to it again
    // is a binding loop. The pane is off screen by now, so a turn of the event
    // loop later is no different to look at.
    function endPop() {
        if (popping)
            Qt.callLater(dropPopped)
    }
    function dropPopped() {
        if (!popping)
            return   // opened again since: this pop was overtaken, not finished
        // Clear before dropping `popping`, or there is an instant with a chat
        // open and nothing popping: a push back to what has just left.
        clearChat()
        popping = false
    }

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

    // Where Ctrl+F lands: an open conversation is what you are most likely
    // looking through, and the list's own box when there is none.
    function startFind() {
        if (currentChatJid !== "")
            chatPage.openSearch()
        else
            conversations.focusSearch()
    }

    onCurrentAccountChanged: {
        // Not a navigation: the open chat belongs to the account just left, so
        // it goes at once rather than sliding off as somebody else's pane.
        // easeSlide goes first: dropping `popping` with the easing still on
        // starts a slide back to the chat that the clear below cannot call off.
        easeSlide = false
        popping = false
        clearChat()
        settleTo(0, 0)   // and whatever was in flight lands where it was headed
        easeSlide = true
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
        if (!wide && currentChatJid !== "") {
            closeChat()
            return true
        }
        return false
    }

    ConversationsPage {
        id: conversations
        // Stacked, drifts left under the arriving chat rather than sitting
        // still, so the two do not read as one sheet.
        x: shell.wide ? 0 : -shell.slide * shell.width * 0.22
        width: shell.listSpan
        height: shell.height
        // Hidden once a chat is open in the narrow layout - but not before the
        // push lands, or there would be no list for the chat to slide over.
        visible: shell.wide || shell.listUp
        account: shell.currentAccount
        onOpenAccounts: railDrawer.open()
        onOpenChat: (jid, name, groupchat) => shell.openChat(jid, name, groupchat)
        onPopOutChat: (jid, name, groupchat) => {
            if (!Theme.mobile)
                AppWindows.popOut(shell.currentAccount, jid, name, groupchat)
        }
        // A hit names its own chat, which need not be the open one. Whether
        // that chat is a room is the chat list's answer, not a reading of the
        // JID.
        onOpenHit: (jid, ts, matches) => {
            if (jid !== shell.currentChatJid) {
                const list = App.chatListFor(shell.currentAccount)
                const entry = list ? list.entryFor(jid) : ({})
                shell.openChat(jid, entry.name ?? "", entry.groupchat === true)
            }
            chatPage.jumpTo(ts, matches)
        }
    }

    // Dim over the list and a soft edge at the chat's leading side, for as long
    // as the push is in flight, so it reads as a layer lifted over the list
    // rather than the two trading places. Declared before the chat, so they
    // land between it and the list.
    Rectangle {
        anchors.fill: parent
        visible: !shell.wide && shell.sliding
        color: "#000000"
        opacity: shell.slide * 0.25
    }
    Rectangle {
        x: chatPage.x - width
        width: 18
        height: shell.height
        visible: !shell.wide && shell.sliding
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#38000000" }
        }
    }

    // Wide, the chat is the column past the divider, taking whatever the list
    // leaves. Stacked, it is a card pushed over the list, so it takes the full
    // width and rides in from the right edge.
    ChatPage {
        id: chatPage
        x: shell.wide ? shell.listSpan + 1 : (1 - shell.slide) * shell.width
        width: shell.wide ? shell.width - shell.listSpan - 1 : shell.width
        height: shell.height
        visible: shell.wide || shell.chatUp
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

    // Dragging from the leading edge takes the push back by hand. Declared after
    // the chat, so the edge is the gesture rather than what it lies over; taps
    // still reach through, since nothing here accepts them.
    Item {
        id: backSwipe
        // Wide enough to reach past the band Android's own edge gesture claims:
        // a strip no wider than that band gets the press and then has it
        // cancelled out from under it, which reads as the swipe going nowhere.
        // The cost is that a reply swipe has to start clear of the strip.
        width: 48
        height: shell.height
        // Whatever the chat has open unwinds first, as it does for handleBack().
        // A slide still in flight is grabbable either way round: `slide` is
        // where it has got to, so the finger picks it up rather than waiting it
        // out - a pop included, which comes back if it is caught in time.
        enabled: (shell.chatOnTop || shell.popping)
                 && !chatPage.searchMode && !chatPage.selectionMode

        // Given up on past this much of the way back, or short of it by a hard
        // enough throw; thrown the other way it is taken back however far it came.
        readonly property real commitAt: 0.65
        readonly property real throwSpeed: 1200
        // How long a reading of the speed stands for. Nothing feeds it once the
        // finger stops, so without this a swipe held half way out and then let
        // go still carries the speed that got it there.
        readonly property int throwWindow: 100

        DragHandler {
            id: backDrag
            // Touch only, as ChatBubble's reply swipe is: a press held at the
            // edge with a mouse is a text selection.
            acceptedDevices: PointerDevice.TouchScreen
            target: null   // we place the panes ourselves
            yAxis.enabled: false
            // Takes the drag off the message list and does not hand it back:
            // wandering off the horizontal is still part of the same swipe.
            grabPermissions: PointerHandler.CanTakeOverFromItems
                             | PointerHandler.CanTakeOverFromHandlersOfDifferentType

            // Where the push stood when the drag began; the handler's
            // translation is measured from there, so catching one in flight
            // carries on from exactly where it had got to.
            property real grabbedSlide: 1

            property real speed: 0
            property real speedAt: 0

            onActiveTranslationChanged: if (active) {
                const carried = activeTranslation.x / shell.width
                shell.slide = Math.max(0, Math.min(1, grabbedSlide - carried))
                speed = centroid.velocity.x
                speedAt = Date.now()
            }
            onActiveChanged: {
                if (active) {
                    // Stop the slide first, or it goes on running under the
                    // finger and the two fight over the same property.
                    shell.holdSlide()
                    grabbedSlide = shell.slide
                    // A pop caught on its way out is called off, so the chat it
                    // was letting go of is still there to be pulled back.
                    shell.popping = false
                    speed = 0
                    speedAt = 0   // the last swipe's speed is not this one's
                    return
                }
                const thrown = Date.now() - speedAt < backSwipe.throwWindow ? speed : 0
                const thrownBack = thrown < -backSwipe.throwSpeed
                const thrownOff = thrown > backSwipe.throwSpeed
                const keep = thrownBack
                             || (!thrownOff && shell.slide > backSwipe.commitAt)
                if (!keep)
                    shell.closeChat()
                // closeChat only says where the push is headed; this is what
                // sends it there, at the speed the finger let go with.
                shell.settleTo(keep ? 1 : 0, Math.abs(thrown))
            }
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
