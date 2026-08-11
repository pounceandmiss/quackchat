pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Quack

// Both the primary window (Main.qml) and what "New window" spawns, so
// several can be open at once, each with its own navigation.
ApplicationWindow {
    id: win
    property alias initialAccount: shell.initialAccount

    width: 1040
    height: 720
    minimumWidth: 360
    minimumHeight: 480
    visible: true
    title: shell.currentAccount !== "" ? "Quack — " + shell.currentAccount
                                       : "Quack Chat"
    color: Theme.background

    Shortcut { sequence: "Ctrl+T"; onActivated: Theme.cycle() }
    Shortcut { sequence: "Ctrl+N"; onActivated: AppWindows.newShell() }
    // `sequences`, not `sequence`: the standard key stands for more than one
    // combination, and binding the singular takes only the first of them.
    Shortcut { sequences: [StandardKey.Find]; onActivated: shell.startFind() }

    // Build the AppWindows singleton now rather than on the first pop-out, so
    // an incoming call gets a window even if nothing else has touched it.
    Component.onCompleted: AppWindows.arm()

    // Android delivers the system back button/gesture as a window close
    // request; step the stacked navigation back instead of quitting while
    // there is somewhere to go. Desktop close (the X) is never intercepted.
    onClosing: (close) => close.accepted = !(Theme.mobile && shell.handleBack())

    AppShell {
        id: shell
        anchors.fill: parent
        // Keep content clear of notches / system bars on mobile; the window's
        // Theme.background still paints behind them. All zeros on desktop.
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
    }
}
