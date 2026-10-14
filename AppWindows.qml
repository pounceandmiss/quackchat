pragma Singleton
// The call delegate reaches the singleton's own id for the one-window rule.
pragma ComponentBehavior: Bound

import QtQuick
import Quack

// Spawns top-level windows on demand and keeps references so the JS engine
// does not garbage-collect a live window. Backend state is shared through the
// App singleton, so extra windows are just extra views.
QtObject {
    id: mgr

    // Nothing binds to these, so they're mutated in place.
    readonly property var _windows: []
    // Every shell window, the primary one included. It is created by the
    // engine rather than by newShell, so it is not in _windows and each shell
    // registers itself instead.
    readonly property var _shells: []
    // account -> its open settings window, so a second request raises it.
    readonly property var _settingsWindows: ({})
    // account|jid -> its open contact window, for the same reason.
    readonly property var _contactWindows: ({})
    // account|jid -> its open room details window, likewise.
    readonly property var _roomWindows: ({})
    // The app's own preferences, which there is only one set of.
    property var _prefsWindow: null

    property Component _shellComp: Component { ShellWindow {} }
    property Component _chatComp: Component { ChatWindow {} }
    property Component _settingsComp: Component { AccountSettingsWindow {} }
    property Component _prefsComp: Component { AppSettingsWindow {} }
    property Component _contactComp: Component { ContactDetailsWindow {} }
    property Component _roomComp: Component { MucDetailsWindow {} }
    property Component _xmlComp: Component { MessageXmlWindow {} }
    property Component _groupInviteComp: Component { GroupCallInviteDialog {} }

    // Call windows are not spawned on demand - they follow App.calls, which is
    // the only record of what is in flight.
    //
    // This lives on the singleton so there is exactly one set of call windows
    // no matter how many shells are open. Singletons are built on first use,
    // and nothing else here runs at startup, so every ShellWindow registers
    // itself below to make sure that happens now rather than whenever
    // something first pops a window out - otherwise the first incoming call
    // would have nowhere to appear, and a notification arriving before any
    // pop-out would have nothing listening.
    function registerShell(w) {
        if (!w)
            return
        mgr._shells.push(w)
        // ShellWindow refuses it when Android's back press had somewhere to go.
        w.closing.connect((close) => {
            if (!close.accepted)
                return
            const i = mgr._shells.indexOf(w)
            if (i >= 0)
                mgr._shells.splice(i, 1)
        })
    }

    // One live call window at a time, as tacky's Tk GUI does it: it keeps a
    // single toplevel and rewires it per call. Here windows follow rows, so the
    // same effect comes from one row at a time holding the screen - "acc\nsid",
    // or "" for none. A ringing call is not in the running: its window shows
    // while it rings regardless, and takes the screen once answered.
    property string _liveCall: ""

    // Named rather than "not ringing and not terminal": `terminal` is a second
    // role and has not necessarily caught up when `state` changes, so a
    // declined call would grab the window on its way out.
    readonly property var _liveStates: ["calling", "ringing", "connecting",
                                        "active"]

    function _callKey(account, sid) { return account + "\n" + sid }

    // Android runs calls on its native CallActivity, so no windows there: a row
    // that ended is dismissed here, as its window would have.
    readonly property bool _nativeCalls: Qt.platform.os === "android"

    property Instantiator _nativeCallRows: Instantiator {
        active: mgr._nativeCalls
        model: App.calls

        delegate: QtObject {
            id: nativeRow
            required property string sid
            required property string account
            required property bool terminal

            function settle() {
                if (nativeRow.terminal)
                    Qt.callLater(App.calls.dismiss, nativeRow.account, nativeRow.sid)
            }
            onTerminalChanged: nativeRow.settle()
            Component.onCompleted: nativeRow.settle()
        }
    }

    property Instantiator _calls: Instantiator {
        active: !mgr._nativeCalls
        model: App.calls

        delegate: QtObject {
            id: row

            required property string sid
            required property string account
            required property string peer
            required property string state
            required property string direction
            required property string warning
            required property string reason
            required property bool terminal
            required property bool offeredVideo
            required property bool hasRemoteVideo
            required property bool sendingVideo
            required property var remoteVideo
            required property var preview
            required property double activeSince
            required property bool verified
            required property string fingerprint

            readonly property string key: mgr._callKey(row.account, row.sid)

            // A call we placed wants the screen at once; one rung at us, once
            // answered.
            function claim() {
                if (mgr._liveStates.indexOf(row.state) >= 0)
                    mgr._liveCall = row.key
            }
            onStateChanged: row.claim()
            Component.onCompleted: row.claim()

            property CallWindow window: CallWindow {
                visible: row.state === "incoming" || mgr._liveCall === row.key
                sid: row.sid
                account: row.account
                peer: row.peer
                state: row.state
                direction: row.direction
                warning: row.warning
                reason: row.reason
                terminal: row.terminal
                offeredVideo: row.offeredVideo
                hasRemoteVideo: row.hasRemoteVideo
                sendingVideo: row.sendingVideo
                remoteVideo: row.remoteVideo
                preview: row.preview
                activeSince: row.activeSince
                verified: row.verified
                fingerprint: row.fingerprint
            }
        }
    }

    // Group calls follow App.groupCalls the same way. Every room the app has
    // heard of has a row, so the window is built for each and shown only while
    // its call has a phase to show - and there is no one-at-a-time rule: two
    // rooms' calls are two walls, not two views of one call.
    property Instantiator _groupCalls: Instantiator {
        active: !mgr._nativeCalls
        model: App.groupCalls

        delegate: QtObject {
            id: groupRow
            required property GroupCall call

            property GroupCallWindow window: GroupCallWindow {
                visible: groupRow.call.phase !== "idle"
                call: groupRow.call
            }
        }
    }

    // Likewise for group calls and GroupCallActivity.
    property Instantiator _nativeGroupCalls: Instantiator {
        active: mgr._nativeCalls
        model: App.groupCalls

        delegate: QtObject {
            id: nativeGroup
            required property GroupCall call
            readonly property bool over: nativeGroup.call.phase === "ended"
            onOverChanged: if (nativeGroup.over) Qt.callLater(nativeGroup.call.dismiss)
        }
    }

    // The banner's way back to a call's window from the chat.
    function raiseGroupCall(account, jid) {
        if (mgr._nativeCalls) {
            const call = App.groupCalls.callFor(account, jid)
            if (call && call.inCall)
                App.showGroupCallScreen(account, call.callJid)
            return null
        }
        const n = mgr._groupCalls.count
        for (let i = 0; i < n; ++i) {
            const row = mgr._groupCalls.objectAt(i)
            if (row && row.call.account === account && row.call.jid === jid
                    && row.window.visible)
                return _raise(row.window)
        }
        return null
    }

    // Someone calling a chat of ours: the dialog rings, and answers the invite
    // stored in that chat. Held like any other spawned window.
    // On Android the service rings it as a notification.
    property Connections _groupInvites: Connections {
        enabled: !mgr._nativeCalls
        target: App.groupCalls
        function onInvited(account, chat, timestamp, room, from, video) {
            mgr._track(mgr._groupInviteComp.createObject(null, {
                account: account, chat: chat, timestamp: timestamp,
                room: room, from: from, video: video }))
        }
    }

    // These windows are held here rather than by the engine, so at shutdown they
    // outlive the singletons they read: their bindings re-run against an App and
    // a Theme that have already gone, and the process dies inside a singleton
    // property lookup. Take them down while what they read is still there.
    property Connections _quit: Connections {
        target: Qt.application
        function onAboutToQuit() { mgr.closeAll() }
    }

    // Every window this object made. The primary shell is the engine's, not
    // ours, and goes down with it.
    function closeAll() {
        for (const w of mgr._windows.slice())
            mgr._forget(w)
    }

    function _track(w) {
        if (!w)
            return null
        mgr._windows.push(w)
        w.closing.connect(() => mgr._forget(w))
        return w
    }

    // Every map above that holds at most one window per key. A key left
    // pointing at a closed window would answer the next request by raising
    // something already destroyed, so _forget clears them all.
    readonly property var _keyedWindows: [mgr._settingsWindows, mgr._contactWindows,
                                          mgr._roomWindows]

    function _forget(w) {
        const i = mgr._windows.indexOf(w)
        if (i >= 0)
            mgr._windows.splice(i, 1)
        for (const held of mgr._keyedWindows)
            for (const key in held)
                if (held[key] === w)
                    delete held[key]
        if (mgr._prefsWindow === w)
            mgr._prefsWindow = null
        // Finish any GC cycle first: a window torn down mid-cycle can crash
        // Qt 6.12's incremental GC in its sweep (seen in our tests; related:
        // https://github.com/arancormonk/dsd-neo/pull/629). A full collection
        // here costs a few ms.
        gc()
        w.destroy()
    }

    // Open another full shell window, optionally pre-selecting an account.
    function newShell(account) {
        return _track(mgr._shellComp.createObject(null,
            { initialAccount: account || "" }))
    }

    // Pop a single conversation out into its own window.
    function popOut(account, jid, name, groupchat) {
        return _track(mgr._chatComp.createObject(null,
            { account: account, chatJid: jid, chatName: name,
              chatGroupchat: groupchat === true }))
    }

    // Account details. Editing the same account from two windows would let one
    // overwrite the other's unsaved form, so there is only ever one open.
    function accountSettings(account) {
        if (!account)
            return null
        const open = mgr._settingsWindows[account]
        if (open)
            return _raise(open)
        const w = _track(mgr._settingsComp.createObject(null, { account: account }))
        if (w)
            mgr._settingsWindows[account] = w
        return w
    }

    // The app's preferences. App-wide, so every window's menu leads to the one
    // window rather than to a view of its own.
    function preferences() {
        if (mgr._prefsWindow)
            return _raise(mgr._prefsWindow)
        mgr._prefsWindow = _track(mgr._prefsComp.createObject(null))
        return mgr._prefsWindow
    }

    // One contact's details. Trust is written as it is picked, so two windows
    // on the same contact would argue over what is on screen. `fingerprint`
    // marks the key a message came from, and moves to the newest one asked
    // about when the window is already open.
    function contactDetails(account, jid, name, fingerprint) {
        if (!account || !jid)
            return null
        const key = account + "|" + jid
        const open = mgr._contactWindows[key]
        if (open) {
            open.fingerprint = fingerprint || ""
            return _raise(open)
        }
        const w = _track(mgr._contactComp.createObject(null,
            { account: account, jid: jid, name: name || "",
              fingerprint: fingerprint || "" }))
        if (w)
            mgr._contactWindows[key] = w
        return w
    }

    // One room's details, for the same reason the contact page above is one per
    // contact: moderation is written from in there, and two windows on the same
    // room would argue over it.
    function mucDetails(account, jid, name) {
        if (!account || !jid)
            return null
        const key = account + "|" + jid
        const open = mgr._roomWindows[key]
        if (open)
            return _raise(open)
        const w = _track(mgr._roomComp.createObject(null,
            { account: account, jid: jid, name: name || "" }))
        if (w)
            mgr._roomWindows[key] = w
        return w
    }

    // One message's stanza. Read-only, so unlike the two above there is nothing
    // for a second window to argue over - any number can be open at once.
    function messageXml(xml) {
        return _track(mgr._xmlComp.createObject(null, { xml: xml || "" }))
    }

    // Where picking a desktop notification lands. A pop-out already holding
    // the chat owns it; otherwise the first shell open switches to it, and
    // failing that a new shell opens on the account.
    //
    // Nothing is marked read here: the chat reaching the screen is what moves
    // the watermark, and that is what retracts the alert.
    function showChat(account, jid) {
        if (!account || !jid)
            return null

        for (const w of mgr._windows) {
            if (w.chatJid === jid && w.account === account)
                return _raise(w)
        }

        const shell = mgr._shells.length > 0 ? mgr._shells[0]
                                             : mgr.newShell(account)
        if (!shell)
            return null
        shell.showChat(account, jid)
        return _raise(shell)
    }

    function _raise(w) {
        w.raise()
        w.requestActivate()
        return w
    }

    // One connection for the whole app, not one per window.
    property Connections _notifications: Connections {
        target: App.notifications
        function onActivated(acc, jid) { mgr.showChat(acc, jid) }
    }
}
