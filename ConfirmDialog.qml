import QtQuick
import QtQuick.Controls
import Quack

// Yes/Cancel over one line of explanation - the Tk GUI's tk_messageBox -type
// yesno, used before anything that throws work away. `subject` rides along so a
// shared instance still knows which row the Yes belongs to.
Dialog {
    id: dlg

    property string message: ""
    property string subject: ""

    // Parented to the overlay, so the width clamp is against the window rather
    // than against whichever pane happened to declare it.
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(340, parent ? parent.width - 24 : 340)
    standardButtons: Dialog.Cancel | Dialog.Yes

    Text {
        objectName: "confirmMessage"
        width: dlg.availableWidth
        text: dlg.message
        color: Theme.textPrimary
        wrapMode: Text.WordWrap
    }
}
