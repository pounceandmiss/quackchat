pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// How an account reaches its server: directly, through Tor, or through a
// SOCKS5 proxy, with one address field under the two that need it. The same
// choice on the account page, at sign-in and at sign-up, so an account starts
// out on the route it will keep.
//
// Each choice has a field of its own, so switching back loses nothing typed.
// pendingProxy and pendingTor are what goes into tacky's `proxy` and `tor`:
// with Tor, `proxy` is where Tor listens, "" for 127.0.0.1:9050.
ColumnLayout {
    id: chooser

    // "direct", "tor" or "proxy".
    property string route: "direct"
    property alias torText: torField.text
    property alias proxyText: proxyField.text

    readonly property string pendingProxy: chooser.route === "tor" ? torField.text.trim()
                                         : chooser.route === "proxy" ? proxyField.text.trim() : ""
    readonly property bool pendingTor: chooser.route === "tor"

    // Return pressed in a field.
    signal accepted

    function clear() {
        chooser.route = "direct"
        torField.clear()
        proxyField.clear()
    }

    Layout.fillWidth: true
    spacing: 12

    ChoiceBox {
        objectName: "routeBox"
        Layout.fillWidth: true
        Accessible.name: qsTr("Connect")
        entries: [
            { label: qsTr("Directly"), value: "direct" },
            { label: qsTr("Through Tor"), value: "tor" },
            { label: qsTr("Through a SOCKS5 proxy"), value: "proxy" }
        ]
        current: chooser.route
        onPicked: (value) => chooser.route = value
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        visible: chooser.route === "tor"
        Caption { text: qsTr("Tor's SOCKS port") }
        TextField {
            id: torField
            objectName: "torField"
            Layout.fillWidth: true
            // Empty is tacky's default address; a login here would be used
            // as given.
            placeholderText: qsTr("socks5://127.0.0.1:9050")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
                              | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            onAccepted: chooser.accepted()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        visible: chooser.route === "proxy"
        Caption { text: qsTr("SOCKS5 proxy") }
        TextField {
            id: proxyField
            objectName: "proxyField"
            Layout.fillWidth: true
            // It can carry a login, so no suggestions.
            placeholderText: qsTr("socks5://host:port")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase
                              | Qt.ImhNoPredictiveText | Qt.ImhUrlCharactersOnly
            onAccepted: chooser.accepted()
        }
    }

    Caption {
        Layout.fillWidth: true
        visible: chooser.route !== "direct"
        text: chooser.route === "tor"
              ? qsTr("Everything this account sends goes through Tor, files included, on circuits no other account shares. Calls are off. Leave the port empty for a system Tor or Orbot; Tor Browser listens on socks5://127.0.0.1:9150.")
              : qsTr("Everything this account sends goes through the proxy, files included. Calls are off while it is set. A login goes in as socks5://user:password@host:port.")
        wrapMode: Text.WordWrap
    }
}
