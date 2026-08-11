pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One endpoint's controls: mute, how loud, and which device. Purely a view - it
// reports what was picked and re-reads the values it is given, so the model
// stays the only thing that knows the current setting.
//
// Mute and volume are what get touched mid-call, so they sit in the row; the
// device list is a once-in-a-while thing and lives behind the ▾ button.
//
// The backend has no mute; muting is gain 0.0, and the level to come back to is
// remembered on the model side.
RowLayout {
    id: row

    property string label: ""
    property string mutedGlyph: "🔇"
    property string liveGlyph: "🔊"
    property var devices: []
    property string deviceId: ""
    property real volume: 1.0
    property bool muted: false

    signal devicePicked(string id)
    signal volumePicked(real volume)
    signal muteToggled(bool muted)

    // "" is a real, selectable value: it means whatever the system picks.
    readonly property var entries: [{ name: qsTr("System default"), id: "" }]
                                       .concat(row.devices)

    spacing: 8

    IconButton {
        text: row.muted ? row.mutedGlyph : row.liveGlyph
        glyphColor: row.muted ? Theme.negative : Theme.textPrimary
        // Which endpoint this is is only in the glyph, so say it here.
        Accessible.name: row.muted ? qsTr("Unmute %1").arg(row.label)
                                   : qsTr("Mute %1").arg(row.label)
        onClicked: row.muteToggled(!row.muted)
    }

    Slider {
        id: vol
        Layout.fillWidth: true
        from: 0.0
        to: 1.0
        Accessible.name: qsTr("%1 volume").arg(row.label)
        // moved(), not valueChanged(), so echoing the model back in does not
        // bounce another setVolume at the backend.
        onMoved: row.volumePicked(value)

        background: Rectangle {
            x: vol.leftPadding
            y: vol.topPadding + vol.availableHeight / 2 - height / 2
            implicitWidth: 84
            implicitHeight: 4
            width: vol.availableWidth
            height: implicitHeight
            radius: 2
            color: Theme.field
            border.color: Theme.hairline

            Rectangle {
                width: vol.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }

        handle: Rectangle {
            x: vol.leftPadding + vol.visualPosition * (vol.availableWidth - width)
            y: vol.topPadding + vol.availableHeight / 2 - height / 2
            implicitWidth: 16
            implicitHeight: 16
            radius: width / 2
            color: vol.pressed ? Theme.accentDeep : Theme.accent
            border.color: Theme.hairline
        }

        // Dragging assigns `value` directly, which would blow away a plain
        // `value: row.volume` binding and leave this slider deaf to changes
        // made anywhere else - another call window, or the gain being pushed
        // by the backend. A Binding survives that.
        Binding {
            target: vol
            property: "value"
            value: row.volume
            restoreMode: Binding.RestoreNone
        }
    }

    IconButton {
        id: pickButton
        text: "▾"
        Accessible.name: qsTr("Choose %1").arg(row.label)
        onClicked: deviceMenu.popup(pickButton, 0, pickButton.height)
    }

    // Drawn from Theme like every other Control here. A stock one follows the
    // system palette instead, which is how the drop-down this replaced ended up
    // painting its text against a background from the other scheme.
    Menu {
        id: deviceMenu
        objectName: "deviceMenu"
        // implicitWidth, not width: a Popup is not an Item, but it is parented
        // into a layout here and a plain `width` reads as a layout override.
        implicitWidth: 280

        background: Rectangle {
            color: Theme.surface
            radius: 10
            border.color: Theme.hairline
        }

        // The device array is replaced wholesale on every re-enumeration, so
        // the entries are instantiated rather than declared.
        Instantiator {
            model: row.entries
            onObjectAdded: (index, object) => deviceMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => deviceMenu.removeItem(object)

            delegate: MenuItem {
                id: entry
                required property var modelData
                readonly property bool current: entry.modelData.id === row.deviceId

                height: 40
                onTriggered: row.devicePicked(entry.modelData.id)

                contentItem: RowLayout {
                    spacing: 6
                    Text {
                        // A fixed column so the names line up whether or not
                        // the tick is there.
                        Layout.preferredWidth: 14
                        text: entry.current ? "✓" : ""
                        color: Theme.accentDeep
                        font.pixelSize: 13
                    }
                    Text {
                        Layout.fillWidth: true
                        text: entry.modelData.name
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: entry.current
                        elide: Text.ElideMiddle
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                background: Rectangle {
                    color: entry.highlighted ? Theme.menuHover : "transparent"
                    radius: 6
                }
            }
        }
    }
}
