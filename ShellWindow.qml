pragma ComponentBehavior: Bound

import QtQuick
import Quack

// Both the primary window (Main.qml) and what "New window" spawns, so
// several can be open at once, each with its own navigation.
AppWindow {
    id: win
    property alias initialAccount: shell.initialAccount

    width: 1040
    height: 720
    minimumWidth: 360
    minimumHeight: 480
    title: shell.currentAccount !== ""
           ? qsTr("Quack — %1").arg(shell.currentAccount)
           : qsTr("Quack Chat")

    Shortcut { sequence: "Ctrl+N"; onActivated: AppWindows.newShell() }
    // `sequences`, not `sequence`: the standard key stands for more than one
    // combination, and binding the singular takes only the first of them.
    Shortcut { sequences: [StandardKey.Find]; onActivated: shell.startFind() }

    // Also builds the AppWindows singleton now rather than on the first
    // pop-out, so an incoming call gets a window even if nothing else has
    // touched it.
    Component.onCompleted: AppWindows.registerShell(win)

    // Where AppWindows sends a chat this window should show, e.g. off a
    // desktop notification.
    function showChat(account, jid) {
        shell.showChat(account, jid)
    }

    // Android delivers the system back button/gesture as a window close
    // request; step the stacked navigation back instead of quitting while
    // there is somewhere to go. Desktop close (the X) is never intercepted.
    onClosing: (close) => close.accepted = !(Theme.mobile && shell.handleBack())

    AppShell {
        id: shell
        anchors.fill: parent
    }

    edgeToEdge: BackendNotice {}
}
