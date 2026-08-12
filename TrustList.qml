pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quack

// A set of OMEMO devices and what we make of each: one fingerprint and picker
// per device, with a "set all" above them once there is more than one to set.
// The account page lists this account's own devices, a contact's keys page
// lists theirs - the same rows either way, so they come from here.
ColumnLayout {
    id: list

    // An OmemoDevicesModel, or null on a page that has no account yet.
    required property var devices
    // The copy is left to the page, which is where the "copied" notice is.
    signal copyRequested(string spaced)

    Layout.fillWidth: true
    spacing: 10

    // Only worth offering once there is more than one to set. The label sits
    // above the control so this picker lines up with the per-device ones.
    ColumnLayout {
        objectName: "setAllRow"
        Layout.fillWidth: true
        Layout.topMargin: 4
        visible: list.devices !== null && list.devices.settableCount >= 2
        spacing: 4
        Caption { text: "Set all"; font.bold: true }
        TrustPicker {
            objectName: "setAllPicker"
            trust: list.devices ? list.devices.commonTrust : ""
            onPicked: (newTrust) => list.devices.setAllTrust(newTrust)
        }
    }

    Repeater {
        objectName: "deviceList"
        model: list.devices

        delegate: ColumnLayout {
            id: deviceRow
            required property int device
            required property string trust
            required property bool active
            required property string fingerprint
            required property bool settable

            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 6

            Fingerprint {
                Layout.fillWidth: true
                hex: deviceRow.fingerprint
                note: deviceRow.active ? "" : "(inactive)"
                onCopyRequested: (spaced) => list.copyRequested(spaced)
            }

            // The backend pins a rotated key, so there is nothing to pick on
            // this row.
            Text {
                Layout.fillWidth: true
                visible: !deviceRow.settable
                text: "Compromised - key changed"
                color: Theme.negative
                font.pixelSize: 12
                font.bold: true
                wrapMode: Text.WordWrap
            }

            TrustPicker {
                objectName: "devicePicker"
                visible: deviceRow.settable
                trust: deviceRow.trust
                onPicked: (newTrust) => list.devices.setTrust(deviceRow.device, newTrust)
            }
        }
    }
}
