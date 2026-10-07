import QtQuick
import QtQuick.Controls
import Quack

// "Show password", for the field beside it to read `checked` from.
CheckBox {
    id: box
    padding: 0
    // The style centres its indicator when the control has no text of its
    // own, so set it even though contentItem draws it.
    text: qsTr("Show password")
    contentItem: Text {
        text: box.text
        color: Theme.textDim
        font.pixelSize: 12
        leftPadding: box.indicator.width + 8
        verticalAlignment: Text.AlignVCenter
    }
}
