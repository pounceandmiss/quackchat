pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// The preferences that belong to the app rather than to an account. Hosted by
// AppSettingsWindow on desktop and as a full-screen sheet on mobile, so it
// carries its own header. Each control writes as it is picked, so there is no
// Save.
Page {
    id: page
    objectName: "appSettingsPage"

    property bool showClose: true
    signal done

    background: Rectangle { color: Theme.background }

    component SectionTitle: Text {
        color: Theme.textPrimary
        font.pixelSize: 18
        font.bold: true
    }

    // One choice out of a group. The dot is bound to the setting rather than
    // toggled by the click, so a write that never lands leaves the row where
    // it was.
    component OptionRow: ItemDelegate {
        id: opt
        property bool selected: false

        Layout.fillWidth: true
        implicitHeight: 36
        padding: 0
        Accessible.role: Accessible.RadioButton
        Accessible.checked: opt.selected

        contentItem: RowLayout {
            spacing: 10
            Rectangle {
                Layout.leftMargin: 2
                implicitWidth: 18
                implicitHeight: 18
                radius: width / 2
                color: "transparent"
                border.width: opt.selected ? 5 : 1
                border.color: opt.selected ? Theme.accent : Theme.textDim
            }
            Text {
                Layout.fillWidth: true
                text: opt.text
                color: Theme.textPrimary
                font.pixelSize: 14
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: opt.hovered ? Theme.menuHover : "transparent"
            radius: 8
        }
    }

    // A theme drawn in its own colors, which is the whole of the preview: the
    // name over the surface it would give the app, beside its accent and its
    // outgoing bubble.
    component ThemeChip: Rectangle {
        id: chip
        required property string themeName
        readonly property var pal: Theme.palettes[chip.themeName]
        readonly property bool current: Theme.name === chip.themeName

        objectName: "theme_" + chip.themeName
        implicitWidth: chipRow.implicitWidth + 20
        implicitHeight: 36
        radius: 10
        color: chip.pal.surface
        border.width: chip.current ? 2 : 1
        border.color: chip.current ? Theme.accent : Theme.hairline

        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                implicitWidth: 12; implicitHeight: 12
                radius: width / 2
                color: chip.pal.accent
            }
            Rectangle {
                implicitWidth: 12; implicitHeight: 12
                radius: width / 2
                color: chip.pal.bubbleOut
                border.width: 1
                border.color: chip.pal.hairline
            }
            Text {
                text: chip.themeName
                color: chip.pal.textPrimary
                font.pixelSize: 13
                font.bold: chip.current
            }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: Theme.name = chip.themeName }
    }

    header: PageHeader {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: page.showClose ? 4 : 16
            anchors.rightMargin: 8
            spacing: 4
            IconButton {
                iconPath: Icons.arrowBack
                Accessible.name: qsTr("Back")
                visible: page.showClose
                glyphColor: Theme.textDim
                onClicked: page.done()
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Preferences")
                color: Theme.textPrimary
                font.pixelSize: 20
                font.bold: true
                elide: Text.ElideRight
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            width: scroll.availableWidth
            spacing: 12

            Item { Layout.preferredHeight: 4 }

            Card {
                SectionTitle { text: qsTr("Images") }
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("What may be fetched without asking. Shared by every account.")
                    wrapMode: Text.WordWrap
                }

                Caption { Layout.topMargin: 4; text: qsTr("Load images") }
                Repeater {
                    model: [
                        { label: qsTr("From everyone"), value: "everyone" },
                        { label: qsTr("From contacts only"), value: "contacts" },
                        { label: qsTr("Never"), value: "never" }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "autofetch_" + modelData.value
                        text: modelData.label
                        selected: App.settings.attachmentAutofetch === modelData.value
                        onClicked: App.settings.setAttachmentAutofetch(modelData.value)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.hairline
                }

                Caption { text: qsTr("Max image size") }
                Repeater {
                    model: [
                        { label: qsTr("1 MB"), value: 1048576 },
                        { label: qsTr("5 MB"), value: 5242880 },
                        { label: qsTr("25 MB"), value: 26214400 },
                        { label: qsTr("Unlimited"), value: 0 }
                    ]
                    delegate: OptionRow {
                        required property var modelData
                        objectName: "autofetchMax_" + modelData.value
                        text: modelData.label
                        selected: App.settings.attachmentAutofetchMax === modelData.value
                        onClicked: App.settings.setAttachmentAutofetchMax(modelData.value)
                    }
                }

                Caption {
                    Layout.fillWidth: true
                    text: qsTr("A picture over the limit waits for a tap instead.")
                    wrapMode: Text.WordWrap
                }
            }

            Card {
                SectionTitle { text: qsTr("Appearance") }

                Flow {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: Object.keys(Theme.palettes)
                        delegate: ThemeChip {
                            required property string modelData
                            themeName: modelData
                        }
                    }
                }

                // Nothing stores the pick yet.
                Caption {
                    Layout.fillWidth: true
                    text: qsTr("The app starts on its own theme each time.")
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.preferredHeight: 4 }
        }
    }
}
