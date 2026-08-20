pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Quack

// One attachment under a message body. An image shows the thumbnail tacky
// derived for it; everything else - a plain file, an image still coming, one
// the autofetch policy held back - shows a chip naming it.
//
// Which of those a tap means is decided here. The menu and every request that
// acts on a file belong to the bubble, so what leaves is a signal.
ColumnLayout {
    id: att
    objectName: "attachment"

    required property int index
    required property var modelData

    property real maxWidth: 0
    property bool selectionMode: false

    signal openRequested()
    signal loadRequested()
    signal menuRequested()

    // What is on disk is what these read, so the neutral ends - never fetched,
    // and the `idle` tacky reports for one it held back, capped or cancelled -
    // need no case of their own. Only `failed` has something extra to say.
    readonly property bool isImage: att.modelData.type === "image"
    readonly property bool hasThumb: att.modelData.thumburl != ""
    readonly property bool busy: att.modelData.state === "active"
    readonly property bool failed: att.modelData.state === "failed"
    readonly property bool sending: att.modelData.direction === "upload"
    readonly property string hint: {
        if (att.busy)
            return att.sending ? qsTr("Uploading…") : qsTr("Downloading…")
        if (att.failed)
            return att.modelData.error !== ""
                ? att.modelData.error
                : (att.sending ? qsTr("Upload failed")
                               : qsTr("Download failed"))
        if (att.isImage && !att.hasThumb)
            return qsTr("Tap to load")
        // tacky knows an outgoing file's size up front; an incoming one's only
        // arrives as the transfer's Content-Length, so either may be the known
        // one.
        return att.fmtSize(att.modelData.size > 0
                           ? att.modelData.size
                           : att.modelData.total)
    }

    // Byte counts as the chip shows them.
    function fmtSize(n) {
        if (!n || n <= 0)
            return ""
        let v = n
        let i = 0
        while (v >= 1024 && i < 3) {
            v /= 1024
            i++
        }
        // A case each rather than a number joined to a unit from a table: the
        // space between the two is not a space in every language.
        const size = i === 0 ? v : v.toFixed(1)
        switch (i) {
        case 0:  return qsTr("%1 B").arg(size)
        case 1:  return qsTr("%1 KB").arg(size)
        case 2:  return qsTr("%1 MB").arg(size)
        default: return qsTr("%1 GB").arg(size)
        }
    }

    // Same rule as the Tk client's: a shown thumbnail means the file is on
    // disk, so a tap opens it; anything else has to be fetched first.
    function activate() {
        if (att.failed || (att.isImage && !att.hasThumb))
            att.loadRequested()
        else
            att.openRequested()
    }

    spacing: 3
    Layout.maximumWidth: att.maxWidth
    Layout.bottomMargin: 3

    // The attachment's own menu, not the message's: a press on a picture is
    // asking about the picture.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: att.menuRequested()
    }
    // ReleaseWithinBounds to grab ahead of the row's own press, which would
    // otherwise select the message and open its menu over this one. Off while
    // selecting, where the row owns the press for the text.
    TapHandler {
        enabled: !att.selectionMode
        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onLongPressed: att.menuRequested()
    }

    Image {
        id: thumb
        objectName: "attachmentThumb"
        visible: att.isImage && att.hasThumb
        // The thumbnail path is derived from the URL alone, so a re-fetched
        // image reuses it - a cached pixmap would keep showing the old one.
        cache: false
        source: att.modelData.thumburl
        fillMode: Image.PreserveAspectFit
        // implicitWidth is source pixels, several per drawn one above ratio 1,
        // so cap at the size asked for.
        readonly property real drawWidth:
            Math.min(thumb.implicitWidth, Theme.thumbSize, att.maxWidth)
        Layout.preferredWidth: thumb.drawWidth
        Layout.preferredHeight: thumb.implicitWidth > 0
            ? thumb.drawWidth * thumb.implicitHeight / thumb.implicitWidth
            : 0
        TapHandler {
            enabled: !att.selectionMode
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: att.activate()
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    Rectangle {
        id: attChip
        objectName: "attachmentChip"
        visible: !thumb.visible
        Layout.preferredWidth: Math.min(attChipRow.implicitWidth + 20, att.maxWidth)
        Layout.preferredHeight: 44
        radius: 10
        color: Theme.field

        RowLayout {
            id: attChipRow
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 8
            Glyph {
                path: att.isImage ? Icons.image : Icons.attachFile
                color: Theme.textDim
                size: 18
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    // Never the url: an aesgcm:// fragment carries the media
                    // key.
                    text: att.modelData.name
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    elide: Text.ElideMiddle
                }
                Text {
                    objectName: "attachmentHint"
                    Layout.fillWidth: true
                    visible: att.hint !== ""
                    text: att.hint
                    color: att.failed ? Theme.negative : Theme.textDim
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }
        TapHandler {
            enabled: !att.selectionMode
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: att.activate()
        }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    // Under whichever of the two is showing, so an image being re-fetched keeps
    // its old thumbnail meanwhile.
    Rectangle {
        objectName: "attachmentProgress"
        visible: att.busy && att.modelData.total > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 3
        radius: 1.5
        color: Theme.hairline
        Rectangle {
            width: parent.width * Math.min(1, att.modelData.loaded / att.modelData.total)
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }
}
