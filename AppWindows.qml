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
        w.closing.connect(() => {
            const i = mgr._shells.indexOf(w)
            if (i >= 0)
                mgr._shells.splice(i, 1)
        })
    }

    // One call window at a time, as tacky's Tk GUI does it: it keeps a single
    // toplevel and rewires it per call. Here windows follow rows, so the same
    // effect comes from one row at a time holding the screen - "acc\nsid", or
    // "" for none. A ringing call is not in the running; it has the dialog, and
    // only takes the window once answered.
    property string _liveCall: ""

    // Named rather than "not ringing and not terminal": `terminal` is a second
    // role and has not necessarily caught up when `state` changes, so a
    // declined call would grab the window on its way out.
    readonly property var _liveStates: ["calling", "ringing", "connecting",
                                        "active"]

    function _callKey(account, sid) { return account + "\n" + sid }

    property Instantiator _calls: Instantiator {
        model: App.calls

        // Both of a row's windows, told apart by which one is visible.
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

            readonly property string key: mgr._callKey(row.account, row.sid)

            // A call we placed wants the window at once; one we answered takes
            // it off the dialog.
            function claim() {
                if (mgr._liveStates.indexOf(row.state) >= 0)
                    mgr._liveCall = row.key
            }
            onStateChanged: row.claim()
            Component.onCompleted: row.claim()

            property IncomingCallDialog dialog: IncomingCallDialog {
                visible: row.state === "incoming"
                sid: row.sid
                account: row.account
                peer: row.peer
            }

            property CallWindow window: CallWindow {
                visible: mgr._liveCall === row.key
                sid: row.sid
                account: row.account
                peer: row.peer
                state: row.state
                direction: row.direction
                warning: row.warning
                reason: row.reason
                terminal: row.terminal
            }
        }
    }

    function _track(w) {
        if (!w)
            return null
        mgr._windows.push(w)
        w.closing.connect(() => mgr._forget(w))
        return w
    }

    function _forget(w) {
        const i = mgr._windows.indexOf(w)
        if (i >= 0)
            mgr._windows.splice(i, 1)
        for (const acc in mgr._settingsWindows)
            if (mgr._settingsWindows[acc] === w)
                delete mgr._settingsWindows[acc]
        for (const key in mgr._contactWindows)
            if (mgr._contactWindows[key] === w)
                delete mgr._contactWindows[key]
        for (const room in mgr._roomWindows)
            if (mgr._roomWindows[room] === w)
                delete mgr._roomWindows[room]
        if (mgr._prefsWindow === w)
            mgr._prefsWindow = null
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
        if (open) {
            open.raise()
            open.requestActivate()
            return open
        }
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
    // on the same contact would argue over what is on screen.
    function contactDetails(account, jid, name) {
        if (!account || !jid)
            return null
        const key = account + "|" + jid
        const open = mgr._contactWindows[key]
        if (open) {
            open.raise()
            open.requestActivate()
            return open
        }
        const w = _track(mgr._contactComp.createObject(null,
            { account: account, jid: jid, name: name || "" }))
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
