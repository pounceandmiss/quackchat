import QtQuick
import QtQuick.Effects
import Quack

// Reading App.avatars.hashFor() both returns the current hash and subscribes
// the JID so the backend fetches it. The hash is the content address in the
// URL, so an avatar update changes the source and QML re-fetches; `rev` is
// read only to give the binding something to depend on.
//
// Many avatars (room logos especially) are transparent PNGs authored to sit
// on white, so once a real picture loads we drop the accent/initial backing
// for a neutral surface with a hairline ring, rather than let a coloured
// circle show through.
Rectangle {
    id: av

    property string account: ""
    property string jid: ""
    property string label: ""
    property int initialsPixelSize: 18

    readonly property bool hasPicture: picture.status === Image.Ready

    implicitWidth: 44
    implicitHeight: 44
    radius: width / 2
    antialiasing: true
    color: hasPicture ? Theme.surface : "transparent"
    border.width: hasPicture ? 1 : 0
    border.color: Theme.hairline

    Rectangle {
        anchors.fill: parent
        radius: av.radius
        antialiasing: true
        visible: !av.hasPicture
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0.0; color: Theme.accent2 }
            GradientStop { position: 1.0; color: Theme.accent }
        }
        Text {
            anchors.centerIn: parent
            text: av.label.length > 0 ? av.label.charAt(0).toUpperCase() : "?"
            color: Theme.textOnAccent
            font.pixelSize: av.initialsPixelSize
            font.bold: true
        }
    }

    Image {
        id: picture
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // Decode at the on-screen pixel size (tacky hands over full-size bytes),
        // and mipmap the downscale so photos don't alias into a crispy mess.
        mipmap: true
        sourceSize.width: Math.round(av.width * Screen.devicePixelRatio)
        sourceSize.height: Math.round(av.height * Screen.devicePixelRatio)
        visible: false // shown through the masked overlay below
        source: {
            const rev = App.avatars.rev
            if (rev < 0 || av.account === "" || av.jid === "")
                return ""
            const hash = App.avatars.hashFor(av.account, av.jid)
            return hash === ""
                ? "" : "image://avatar/" + av.account + "/" + av.jid + "/" + hash
        }
    }
    MultiEffect {
        anchors.fill: parent
        source: picture
        maskEnabled: true
        maskSource: mask
        visible: av.hasPicture
    }
    Item {
        id: mask
        anchors.fill: parent
        // A Rectangle's vertex antialiasing is lost once rendered into this
        // layer's FBO unless it's multisampled, leaving a hard 1-bit rim.
        layer.enabled: true
        layer.smooth: true
        layer.samples: 4
        visible: false
        Rectangle { anchors.fill: parent; radius: av.radius; antialiasing: true }
    }
}
