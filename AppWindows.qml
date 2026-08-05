pragma Singleton

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

    property Component _shellComp: Component { ShellWindow {} }
    property Component _chatComp: Component { ChatWindow {} }
    property Component _settingsComp: Component { AccountSettingsWindow {} }

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
}
