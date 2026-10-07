pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// A drop-down: Controls' ComboBox, for the behaviour (a click on the open box
// closes it, arrow keys, Escape, accessibility), drawn in the app's palette
// rather than the system one the stock style paints its list from - which put
// the list's text against a background from the other scheme.
//
// The choice shown is bound to `current` rather than set by the pick, so a
// write that never lands leaves the box where it was; `picked` reports the
// pick for whoever owns the value to apply.
ComboBox {
    id: box

    // [{ label, value }]
    property var entries: []
    property var current
    // Device names run long and differ at the end; most labels do neither.
    property int elide: Text.ElideRight

    signal picked(var value)

    model: box.entries
    textRole: "label"
    valueRole: "value"
    currentIndex: box.indexOf(box.current)
    implicitHeight: 34

    function indexOf(value) {
        for (let i = 0; i < box.entries.length; ++i)
            if (box.entries[i].value === value)
                return i
        return -1
    }

    onActivated: (index) => {
        box.picked(box.entries[index].value)
        // The activation wrote currentIndex and so broke the binding; put it
        // back, so the box follows the value rather than the click.
        box.currentIndex = Qt.binding(() => box.indexOf(box.current))
    }

    background: Rectangle {
        color: box.hovered || box.down ? Theme.menuHover : Theme.field
        radius: 8
        border.color: box.visualFocus ? Theme.accent : Theme.hairline
    }

    contentItem: Text {
        leftPadding: 10
        rightPadding: box.indicator.width + 4
        text: box.displayText
        color: Theme.textPrimary
        font.pixelSize: 14
        elide: box.elide
        verticalAlignment: Text.AlignVCenter
    }

    indicator: Glyph {
        x: box.width - width - 6
        y: (box.height - height) / 2
        path: Icons.arrowDropDown
        color: Theme.textDim
        size: 20
    }

    delegate: ItemDelegate {
        id: entry
        required property int index
        required property var modelData
        readonly property bool current: entry.index === box.currentIndex

        width: ListView.view ? ListView.view.width : implicitWidth
        height: 40
        highlighted: box.highlightedIndex === entry.index

        contentItem: RowLayout {
            spacing: 6
            Glyph {
                // A fixed column so the labels line up whether or not the tick
                // is there: an empty path draws nothing but stays laid out.
                Layout.preferredWidth: 14
                path: entry.current ? Icons.check : ""
                color: Theme.accentDeep
                size: 14
            }
            Text {
                Layout.fillWidth: true
                text: entry.modelData.label
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: entry.current
                elide: box.elide
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            color: entry.highlighted || entry.hovered ? Theme.menuHover : "transparent"
            radius: 6
        }
    }

    popup: Popup {
        // Under the box, at least as wide, and kept inside what can be seen of
        // an edge-to-edge Android window, as AppMenu is.
        readonly property Item windowOverlay: Overlay.overlay
        y: box.height + 2
        width: Math.max(box.width, 200)
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        padding: 4
        topMargin: windowOverlay ? windowOverlay.SafeArea.margins.top : 0
        bottomMargin: windowOverlay ? windowOverlay.SafeArea.margins.bottom : 0
        leftMargin: windowOverlay ? windowOverlay.SafeArea.margins.left : 0
        rightMargin: windowOverlay ? windowOverlay.SafeArea.margins.right : 0

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.hairline
        }
    }
}
