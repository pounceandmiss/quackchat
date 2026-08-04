pragma Singleton

import QtQuick
import Quack

// Spawns top-level windows on demand and keeps references so the JS engine
// does not garbage-collect a live window. Backend state is shared through the
// App singleton, so extra windows are just extra views.
QtObject {
    id: mgr

    // Nothing binds to this, so it's mutated in place.
    readonly property var _windows: []

    property Component _shellComp: Component { ShellWindow {} }
    property Component _chatComp: Component { ChatWindow {} }

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
}
