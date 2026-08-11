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
    // account -> its open settings window, so a second request raises it.
    readonly property var _settingsWindows: ({})
    // account|jid -> its open key window, for the same reason.
    readonly property var _keysWindows: ({})

    property Component _shellComp: Component { ShellWindow {} }
    property Component _chatComp: Component { ChatWindow {} }
    property Component _settingsComp: Component { AccountSettingsWindow {} }
    property Component _keysComp: Component { OmemoKeysWindow {} }

    // Call windows are not spawned on demand - they follow App.calls, which is
    // the only record of what is in flight.
    //
    // This lives on the singleton so there is exactly one set of call windows
    // no matter how many shells are open. Singletons are built on first use,
    // and nothing else here runs at startup, so ShellWindow calls arm() to make
    // sure that happens now rather than whenever something first pops a window
    // out - otherwise the first incoming call would have nowhere to appear.
    function arm() {}

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
        for (const key in mgr._keysWindows)
            if (mgr._keysWindows[key] === w)
                delete mgr._keysWindows[key]
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

    // One contact's OMEMO keys. Trust is written as it is picked, so two
    // windows on the same contact would argue over what is on screen.
    function omemoKeys(account, jid, name) {
        if (!account || !jid)
            return null
        const key = account + "|" + jid
        const open = mgr._keysWindows[key]
        if (open) {
            open.raise()
            open.requestActivate()
            return open
        }
        const w = _track(mgr._keysComp.createObject(null,
            { account: account, jid: jid, name: name || "" }))
        if (w)
            mgr._keysWindows[key] = w
        return w
    }
}
