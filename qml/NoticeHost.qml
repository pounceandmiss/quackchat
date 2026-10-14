pragma ComponentBehavior: Bound

import QtQuick
import Quack

// The one place a window says something that has no home on the page: the
// backend gone or never up, a request turned down, something broken in the
// background. A stack of Notice cards across the top, newest first, over
// whatever is there - a page that moved down for every one of them would be
// worse than a card swiped off it. AppWindow carries one; reach it as
// `Window.window.notices`.
//
// Two kinds of card:
//  - a state (show/hide): up while it is true. Swiped away it stays away until
//    it says something else, or until it was hidden and comes back.
//  - a one-off (post): takes itself away after `timeout`, not counting the
//    time a finger or pointer is on it. Posting again under the same key
//    refreshes that card instead of stacking a copy.
Item {
    id: host

    property int timeout: 8000

    // key -> text of a state card the user swiped away.
    property var _dismissed: ({})
    // key -> what a card's action button runs. A ListModel holds no functions.
    property var _actions: ({})
    property int _serial: 0

    function _indexOf(key) {
        for (let i = 0; i < entries.count; ++i) {
            if (entries.get(i).key === key)
                return i
        }
        return -1
    }

    function _put(key, opts, oneOff) {
        const row = {
            key: key,
            text: opts.text ?? "",
            tone: opts.tone ?? "warning",
            copyable: opts.copyable ?? false,
            copyText: opts.copyText ?? (opts.text ?? ""),
            actionText: opts.actionText ?? "",
            oneOff: oneOff,
            stamp: ++host._serial
        }
        const i = host._indexOf(key)
        if (i < 0)
            entries.insert(0, row)
        else
            entries.set(i, row)
    }

    function show(key, opts) {
        if (host._dismissed[key] === opts.text)
            return
        delete host._dismissed[key]
        host._put(key, opts, false)
    }

    function hide(key) {
        delete host._dismissed[key]
        host._remove(key)
    }

    // Returns the card's key.
    function post(opts, onAction) {
        const key = opts.key ?? ("post" + (host._serial + 1))
        if (onAction)
            host._actions[key] = onAction
        else
            delete host._actions[key]
        host._put(key, opts, true)
        return key
    }

    function dismiss(key) {
        const i = host._indexOf(key)
        if (i < 0)
            return
        if (!entries.get(i).oneOff)
            host._dismissed[key] = entries.get(i).text
        host._remove(key)
    }

    function _remove(key) {
        delete host._actions[key]
        const i = host._indexOf(key)
        if (i >= 0)
            entries.remove(i)
    }

    ListModel { id: entries }

    Column {
        id: stack
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        // Clear of a notch or status bar; zero on desktop.
        anchors.topMargin: 10 + host.SafeArea.margins.top
        width: Math.min(host.width - 24, 420)
        spacing: 8

        Repeater {
            model: entries
            delegate: Notice {
                id: card
                // Through `model` rather than a required property per role:
                // most roles share a name with a property Notice already has.
                required property var model
                readonly property string key: card.model.key
                readonly property bool oneOff: card.model.oneOff
                readonly property int stamp: card.model.stamp

                objectName: "notice_" + card.key
                width: stack.width
                text: card.model.text
                tone: card.model.tone
                copyable: card.model.copyable
                copyText: card.model.copyText
                actionText: card.model.actionText

                onDismissed: host.dismiss(card.key)
                onCopied: {
                    copiedNotice.text = qsTr("Copied")
                    copiedNotice.show()
                }
                onAction: {
                    const run = host._actions[card.key]
                    host.dismiss(card.key)
                    if (run)
                        run()
                }

                Timer {
                    id: expiry
                    running: card.oneOff && !card.held
                    interval: host.timeout
                    onTriggered: host.dismiss(card.key)
                }
                // A repost of the same key starts its time over.
                onStampChanged: if (expiry.running) expiry.restart()
            }
        }
    }

    CopiedNotice {
        id: copiedNotice
        anchors.top: stack.bottom
        anchors.topMargin: 8
        anchors.horizontalCenter: parent.horizontalCenter
    }
}
