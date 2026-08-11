pragma ComponentBehavior: Bound

import QtQuick
import Quack

// OMEMO trust as one segmented control. Trust is three-way, so a switch would
// have to lie about "undecided"; an empty `trust` leaves the thumb off the
// control, which is what the set-all row shows when the devices disagree.
//
// The thumb can be tapped to or dragged between the three positions. Segments
// are one width, so pickers in a column line up and picking never reflows one.
Rectangle {
    id: picker

    property string trust: ""
    // Compromised devices can't be moved, so the control only shows where they are.
    property bool interactive: true
    signal picked(string newTrust)

    readonly property var options: [
        { value: "trusted", label: "Trust" },
        { value: "undecided", label: "Undecided" },
        { value: "untrusted", label: "Don't trust" }
    ]

    readonly property real inset: 4
    readonly property int labelSize: 12
    // The picked label is bold, which is the widest any of them gets, so every
    // segment is that wide and picking one never reflows the row.
    readonly property real segmentWidth: {
        let widest = 0
        for (const option of picker.options)
            widest = Math.max(widest, boldLabel.advanceWidth(option.label))
        return Math.ceil(widest) + 28
    }

    readonly property int currentIndex: {
        for (let i = 0; i < picker.options.length; ++i)
            if (picker.options[i].value === picker.trust)
                return i
        return -1
    }
    // Mid-drag the labels follow the thumb, to preview what releasing picks.
    readonly property int activeIndex: dragHandler.active ? picker.indexAt(thumb.x)
                                                          : picker.currentIndex

    function colorFor(value) {
        switch (value) {
        case "trusted": return Theme.positive
        case "untrusted": return Theme.negative
        default: return Theme.warning
        }
    }
    function xForIndex(i) {
        return picker.inset + Math.max(0, i) * picker.segmentWidth
    }
    function indexAt(x) {
        const i = Math.round((x - picker.inset) / picker.segmentWidth)
        return Math.max(0, Math.min(picker.options.length - 1, i))
    }
    // Move first, then write: the trust that comes back either confirms the
    // thumb's new position or settles it home.
    function choose(i) {
        thumb.x = picker.xForIndex(i)
        if (picker.options[i].value !== picker.trust)
            picker.picked(picker.options[i].value)
    }
    // Where the thumb sits when nothing is dragging it, which moves both when
    // the trust changes and when the segments are resized.
    readonly property real restingX: picker.xForIndex(picker.currentIndex)
    function settle() {
        if (!dragHandler.active)
            thumb.x = picker.restingX
    }

    onRestingXChanged: picker.settle()
    Component.onCompleted: picker.settle()

    implicitWidth: picker.segmentWidth * picker.options.length + 2 * picker.inset
    implicitHeight: 34
    radius: height / 2
    color: Theme.field
    border.width: 1
    border.color: Theme.hairline
    opacity: picker.interactive ? 1 : 0.6

    FontMetrics {
        id: boldLabel
        font.pixelSize: picker.labelSize
        font.bold: true
    }

    Rectangle {
        id: thumb
        objectName: "trustThumb"
        y: picker.inset
        width: picker.segmentWidth
        height: picker.height - 2 * picker.inset
        radius: height / 2
        visible: picker.currentIndex >= 0 || dragHandler.active

        readonly property color tint: picker.colorFor(
            picker.options[Math.max(0, picker.activeIndex)].value)
        // Tinted rather than filled, so the label stays readable in every palette.
        color: Qt.rgba(tint.r, tint.g, tint.b, dragHandler.active ? 0.28 : 0.18)
        border.width: 1
        border.color: tint

        // Off while dragging, or the thumb animates towards the finger instead
        // of following it.
        Behavior on x {
            enabled: !dragHandler.active
            SpringAnimation { spring: 4; damping: 0.4; mass: 0.8; epsilon: 0.25 }
        }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on border.color { ColorAnimation { duration: 140 } }
    }

    DragHandler {
        id: dragHandler
        target: thumb
        // Nothing to drag when no segment is picked; a tap starts it off.
        enabled: picker.interactive && picker.currentIndex >= 0
        yAxis.enabled: false
        xAxis.minimum: picker.xForIndex(0)
        xAxis.maximum: picker.xForIndex(picker.options.length - 1)
        cursorShape: Qt.ClosedHandCursor
        onActiveChanged: if (!active) picker.choose(picker.indexAt(thumb.x))
    }

    Row {
        anchors.fill: parent
        anchors.margins: picker.inset
        spacing: 0

        Repeater {
            model: picker.options

            delegate: Item {
                id: seg
                required property int index
                required property var modelData
                readonly property bool current: picker.activeIndex === seg.index

                width: picker.segmentWidth
                // Sized off the picker rather than the row: a delegate outlives
                // its parent binding on teardown, and the row is just this inset.
                height: picker.height - 2 * picker.inset

                Text {
                    anchors.centerIn: parent
                    text: seg.modelData.label
                    color: seg.current ? picker.colorFor(seg.modelData.value) : Theme.textDim
                    font.pixelSize: picker.labelSize
                    font.bold: seg.current
                    Behavior on color { ColorAnimation { duration: 140 } }
                }

                HoverHandler {
                    enabled: picker.interactive
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    enabled: picker.interactive
                    onTapped: picker.choose(seg.index)
                }
            }
        }
    }
}
