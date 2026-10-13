import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// Every emoji, to pick one: a search field, a tab per group, and one long grid
// the tabs jump along and follow. What was used last leads the grid, and an
// emoji with skin tones offers them on a long press (or a right click), the
// one picked becoming what the grid shows for it.
//
// Hosted by EmojiPopup, which decides where it floats; this is the same on
// desktop and touch apart from sizes, and that the field takes the focus only
// where typing into it needs no keyboard to come up first.
Item {
    id: picker

    signal chosen(string emoji)

    // Fingers rather than a mouse: bigger cells, no hover strip, and no focus
    // for the field until it is tapped, since focus there raises a keyboard.
    property bool touch: Theme.mobile

    readonly property int cellSize: picker.touch ? 46 : 38
    readonly property int glyphSize: picker.touch ? 30 : 25

    // The group the top of the grid is in, which the tabs light.
    property string current: ""

    // Back to how it opens: no search, at the top, the field ready on desktop.
    function reset() {
        search.text = ""
        tones.close()
        grid.positionViewAtBeginning()
        picker.current = grid.sectionAt(0)
        if (!picker.touch)
            search.forceActiveFocus()
    }

    function pick(emoji) {
        emojiModel.use(emoji)
        picker.chosen(emoji)
    }

    // The best match, so a search can end on Enter without the mouse.
    function pickFirst() {
        const first = emojiModel.firstMatch()
        if (first !== "")
            picker.pick(first)
    }

    // How the tabs and headers name a group, and what the tab draws.
    readonly property var groups: ({
        recent:     { title: qsTr("Recently used"),     icon: Icons.schedule },
        smileys:    { title: qsTr("Smileys & emotion"), icon: Icons.emojiEmotions },
        people:     { title: qsTr("People & body"),     icon: Icons.emojiPeople },
        nature:     { title: qsTr("Animals & nature"),  icon: Icons.emojiNature },
        food:       { title: qsTr("Food & drink"),      icon: Icons.emojiFoodBeverage },
        travel:     { title: qsTr("Travel & places"),   icon: Icons.emojiTransportation },
        activities: { title: qsTr("Activities"),        icon: Icons.emojiEvents },
        objects:    { title: qsTr("Objects"),           icon: Icons.emojiObjects },
        symbols:    { title: qsTr("Symbols"),           icon: Icons.emojiSymbols },
        flags:      { title: qsTr("Flags"),             icon: Icons.emojiFlags }
    })

    EmojiModel {
        id: emojiModel
        settings: App.settings
        filter: search.text
        columns: Math.max(1, Math.floor(grid.width / picker.cellSize))
        // A search has no groups; the tabs keep the last ones it had.
        onSectionsChanged: if (emojiModel.sections.length > 0) picker.tabs = emojiModel.sections
        Component.onCompleted: picker.tabs = emojiModel.sections
    }
    property var tabs: []

    // The emoji under the pointer, for the strip along the bottom. Touch has
    // no hover, and there the strip is not drawn at all.
    property var hoveredCell: null

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TextField {
            id: search
            objectName: "emojiSearch"
            Layout.fillWidth: true
            Layout.margins: 10
            Layout.bottomMargin: 6
            placeholderText: qsTr("Search emoji")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            leftPadding: searchGlyph.x + searchGlyph.width + 6
            rightPadding: clearSearch.visible ? clearSearch.width + 6 : leftPadding
            Glyph {
                id: searchGlyph
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                path: Icons.search
                color: Theme.textDim
                size: 16
            }
            IconButton {
                id: clearSearch
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                iconPath: Icons.close
                iconSize: 16
                Accessible.name: qsTr("Clear search")
                glyphColor: Theme.textDim
                visible: search.text !== ""
                focusPolicy: Qt.NoFocus
                onClicked: search.clear()
            }
            // The best match, so a search can end without the mouse.
            Keys.onReturnPressed: picker.pickFirst()
            Keys.onEnterPressed: picker.pickFirst()
        }

        // One tab per group the grid has, the current one lit and underlined.
        // Dimmed while searching, since the matches have no groups, but still
        // a way back: a tab drops the search and goes to its group.
        Item {
            id: tabBar
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            opacity: search.text === "" ? 1 : 0.35
            Behavior on opacity { NumberAnimation { duration: 120 } }

            readonly property real tabWidth: tabRow.count > 0 ? width / tabRow.count : 0

            Row {
                anchors.fill: parent
                Repeater {
                    id: tabRow
                    model: picker.tabs
                    delegate: AbstractButton {
                        id: tab
                        required property var modelData
                        objectName: "emojiTab_" + tab.modelData.section
                        width: tabBar.tabWidth
                        height: tabBar.height
                        focusPolicy: Qt.NoFocus
                        readonly property bool lit: picker.current === tab.modelData.section
                        Accessible.name: picker.groups[tab.modelData.section].title
                        ToolTip.visible: tab.hovered && !picker.touch
                        ToolTip.delay: 600
                        ToolTip.text: picker.groups[tab.modelData.section].title
                        onClicked: {
                            // Cleared first: the rows it jumps to come back with it.
                            search.text = ""
                            grid.jumpTo(tab.modelData.row, tab.modelData.section)
                        }
                        background: Rectangle {
                            anchors.fill: parent
                            anchors.margins: 3
                            radius: 6
                            color: tab.hovered ? Theme.menuHover : "transparent"
                        }
                        contentItem: Item {
                            Glyph {
                                anchors.centerIn: parent
                                path: picker.groups[tab.modelData.section].icon
                                size: 20
                                color: tab.lit ? Theme.accentDeep : Theme.textDim
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                        }
                    }
                }
            }

            // Slides under the lit tab rather than jumping, which is what
            // makes following the scroll read as following it.
            Rectangle {
                id: underline
                readonly property int at: {
                    const s = picker.tabs
                    for (let i = 0; i < s.length; ++i)
                        if (s[i].section === picker.current)
                            return i
                    return -1
                }
                visible: underline.at >= 0 && search.text === ""
                y: parent.height - height
                height: 2
                radius: 1
                width: Math.max(0, tabBar.tabWidth - 16)
                x: underline.at * tabBar.tabWidth + 8
                color: Theme.accentDeep
                Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
        }

        ListView {
            id: grid
            objectName: "emojiGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            clip: true
            model: emojiModel
            boundsBehavior: Flickable.StopAtBounds
            // A long list of small pictures: keep a screen's worth either side
            // built, so a fling does not outrun the delegates.
            cacheBuffer: 600
            ScrollBar.vertical: ThinScrollBar {}

            readonly property real cellWidth: grid.width / emojiModel.columns

            // The group a row belongs to, by the headers that precede it.
            function sectionAt(row) {
                const s = emojiModel.sections
                let found = s.length > 0 ? s[0].section : ""
                for (let i = 0; i < s.length && s[i].row <= row; ++i)
                    found = s[i].section
                return found
            }

            // Lights the tab at once, rather than after the scroll it starts
            // has passed every group in between.
            function jumpTo(row, section) {
                picker.current = section
                grid.following = false
                grid.positionViewAtIndex(row, ListView.Beginning)
                grid.following = true
            }

            property bool following: true
            onContentYChanged: {
                if (!grid.following || search.text !== "")
                    return
                const row = grid.indexAt(0, grid.contentY + 1)
                if (row >= 0)
                    picker.current = grid.sectionAt(row)
            }
            onCountChanged: if (!grid.moving) picker.current = grid.sectionAt(0)

            delegate: Item {
                id: row
                required property bool header
                required property string section
                required property var cells
                width: grid.width
                height: row.header ? 30 : picker.cellSize

                Text {
                    visible: row.header
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    text: row.header ? picker.groups[row.section].title : ""
                    color: Theme.textDim
                    font.pixelSize: 12
                    font.bold: true
                }

                Row {
                    visible: !row.header
                    Repeater {
                        model: row.header ? [] : row.cells
                        delegate: EmojiCell {
                            required property var modelData
                            cell: modelData
                            width: grid.cellWidth
                            height: picker.cellSize
                        }
                    }
                }
            }

            Text {
                objectName: "emojiNoMatches"
                anchors.centerIn: parent
                visible: search.text !== "" && grid.count === 0
                text: qsTr("No emoji found")
                color: Theme.textDim
                font.pixelSize: 14
            }
        }

        // What the pointer is over, big enough to see and with its name:
        // the grid alone is a wall of small pictures, some much alike.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.hairline
            visible: preview.visible
        }
        Item {
            id: preview
            visible: !picker.touch
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Text {
                id: previewGlyph
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                text: picker.hoveredCell ? picker.hoveredCell.emoji : ""
                font.pixelSize: 24
            }
            Text {
                objectName: "emojiPreviewName"
                anchors.left: previewGlyph.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: picker.hoveredCell
                      ? picker.hoveredCell.name + (picker.hoveredCell.toned
                            ? "  ·  " + qsTr("right-click for skin tones") : "")
                      : qsTr("Pick an emoji")
                color: picker.hoveredCell ? Theme.textPrimary : Theme.textDim
                font.pixelSize: 13
            }
        }
    }

    // One emoji in the grid. A dot in the corner marks one with tones to offer.
    component EmojiCell: AbstractButton {
        id: cellButton
        property var cell
        objectName: "emojiCell"
        focusPolicy: Qt.NoFocus
        hoverEnabled: !picker.touch
        Accessible.name: cellButton.cell.name
        onHoveredChanged: {
            if (cellButton.hovered)
                picker.hoveredCell = cellButton.cell
            else if (picker.hoveredCell === cellButton.cell)
                picker.hoveredCell = null
        }
        onClicked: picker.pick(cellButton.cell.emoji)
        onPressAndHold: cellButton.offerTones()
        function offerTones() {
            if (cellButton.cell.toned)
                tones.offer(cellButton, cellButton.cell)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: cellButton.offerTones()
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) - 2
            height: width
            radius: width / 2
            color: cellButton.pressed ? Theme.selection
                 : cellButton.hovered ? Theme.menuHover : "transparent"
        }
        contentItem: Item {
            Text {
                anchors.centerIn: parent
                text: cellButton.cell.emoji
                // Grown in the font, not by scale: emoji are bitmaps, and a
                // scaled one magnifies the raster cached at the smaller size.
                property real grow: cellButton.hovered ? 1.15 : 1.0
                font.pixelSize: Math.round(picker.glyphSize * grow)
                Behavior on grow { NumberAnimation { duration: 90; easing.type: Easing.OutBack } }
            }
            Rectangle {
                visible: cellButton.cell.toned
                width: 4
                height: 4
                radius: 2
                color: Theme.textDim
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: Math.max(2, (parent.width - picker.cellSize) / 2 + 4)
                anchors.bottomMargin: 4
            }
        }
    }

    // The tones of one emoji, floated over the cell it came from: one row of
    // the plain form and five tones, or for a pair the 5x5 square, a row per
    // first person's tone.
    Popup {
        id: tones
        objectName: "emojiTones"
        property var cell: ({ base: "", emoji: "" })
        property var forms: []
        readonly property bool square: tones.forms.length === 25
        padding: 6
        margins: 6
        modal: false
        closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

        function offer(item, cell) {
            tones.cell = cell
            tones.forms = emojiModel.variants(cell.base)
            if (tones.forms.length === 0)
                return
            // Centred over the cell and above it, kept inside the picker.
            const p = item.mapToItem(picker, item.width / 2, 0)
            tones.x = Math.max(0, Math.min(picker.width - tones.implicitWidth,
                                           p.x - tones.implicitWidth / 2))
            tones.y = Math.max(0, p.y - tones.implicitHeight - 4)
            tones.open()
        }

        function take(emoji) {
            emojiModel.useTone(tones.cell.base, emoji)
            tones.close()
            picker.chosen(emoji)
        }

        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.hairline
        }
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 100 }
            NumberAnimation { property: "scale"; from: 0.85; to: 1; duration: 140; easing.type: Easing.OutBack }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
        }

        contentItem: Column {
            spacing: 4
            // The plain form leads, so a tone once picked can be put back.
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                ToneChoice { emoji: tones.cell.base }
                Repeater {
                    model: tones.square ? [] : tones.forms
                    delegate: ToneChoice { required property string modelData; emoji: modelData }
                }
            }
            Grid {
                visible: tones.square
                columns: 5
                Repeater {
                    model: tones.square ? tones.forms : []
                    delegate: ToneChoice { required property string modelData; emoji: modelData }
                }
            }
        }
    }

    component ToneChoice: AbstractButton {
        id: tone
        property string emoji
        objectName: "toneChoice"
        width: picker.cellSize
        height: picker.cellSize
        focusPolicy: Qt.NoFocus
        hoverEnabled: !picker.touch
        Accessible.name: tone.emoji
        onClicked: tones.take(tone.emoji)
        background: Rectangle {
            radius: width / 2
            // The one in use is the one marked.
            color: tone.hovered || tone.pressed ? Theme.menuHover : "transparent"
            border.width: tone.emoji === tones.cell.emoji ? 1.5 : 0
            border.color: Theme.accent
        }
        contentItem: Item {
            Text {
                anchors.centerIn: parent
                text: tone.emoji
                font.pixelSize: picker.glyphSize
            }
        }
    }
}
