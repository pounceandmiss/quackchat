import QtQuick
import QtQuick.Templates as T

// The fallback styles' boxes make their standard buttons out of their own
// Button, so a box of ours is what gives a dialog's OK and Cancel the app's.
// The order is still the platform's: the template lays the roles out by its
// button layout. The accept is the pill (see Button), the rest are text.
T.DialogButtonBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    spacing: 8
    padding: 12
    alignment: Qt.AlignRight

    delegate: Button { flat: true }

    contentItem: ListView {
        implicitWidth: contentWidth
        model: control.contentModel
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        snapMode: ListView.SnapToItem
    }

    background: null
}
