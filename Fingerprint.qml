pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quack

// A key, its caption, and click-to-copy. The groups sit in equal columns across
// the full width, so a key reads down as well as across - which is how two of
// them get compared.
//
// Copying is the host's to do: QML reaches the clipboard through a TextEdit,
// and one per key on a page full of them would be waste. The host also owns
// whatever it shows to say the copy happened.
ColumnLayout {
    id: fp
    property string hex: ""
    property string note: ""
    signal copyRequested(string spaced)
    spacing: 2

    readonly property int groupSize: 13
    readonly property int groupGap: 10
    // 8-char groups, the shape fingerprints are compared in.
    readonly property var groups: {
        const clean = (fp.hex || "").replace(/\s+/g, "")
        let out = []
        for (let i = 0; i < clean.length; i += 8)
            out.push(clean.substr(i, 8))
        return out
    }

    // The fewest rows the width allows, then evened out across them: a stub
    // last row makes two keys harder to compare line by line.
    function columnsFor(available, groupWidth, gap, count) {
        if (count <= 0 || groupWidth <= 0)
            return 1
        const fits = Math.max(1, Math.floor((available + gap) / (groupWidth + gap)))
        const rows = Math.ceil(count / Math.min(fits, count))
        return Math.ceil(count / rows)
    }

    TextMetrics {
        id: groupMetrics
        font.family: "monospace"
        font.pixelSize: fp.groupSize
        text: "00000000"
    }

    // The Grid is wrapped rather than laid out directly: its columns are read
    // off the width it is handed, while a positioner's implicitWidth is
    // measured from its children, so in a layout the two chase each other -
    // the recursive rearrange the layout aborts on. This wrapper owes its
    // width to nothing, which cuts the loop.
    Item {
        id: gridBox
        Layout.fillWidth: true
        Layout.preferredHeight: grid.implicitHeight

        Grid {
            id: grid
            objectName: "fingerprintGroups"
            anchors.left: parent.left
            anchors.right: parent.right
            columns: fp.columnsFor(gridBox.width, groupMetrics.width, fp.groupGap,
                                   fp.groups.length)
            columnSpacing: fp.groupGap
            rowSpacing: 2
            readonly property real cellWidth:
                (gridBox.width - grid.columnSpacing * (grid.columns - 1)) / grid.columns

            Repeater {
                model: fp.groups
                delegate: Text {
                    required property string modelData
                    objectName: "fingerprintGroup"
                    width: grid.cellWidth
                    text: modelData
                    color: Theme.textPrimary
                    font.family: "monospace"
                    font.pixelSize: fp.groupSize
                }
            }
        }
    }
    RowLayout {
        spacing: 8
        Text {
            text: "OMEMO fingerprint"
            color: Theme.textDim
            font.pixelSize: 11
            font.bold: true
        }
        Text {
            text: fp.note
            visible: fp.note !== ""
            color: Theme.textDim
            font.pixelSize: 11
        }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: fp.copyRequested(fp.groups.join(" ")) }
}
