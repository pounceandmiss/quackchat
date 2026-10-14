import QtQuick
import QtQuick.Controls
import Quack

// Yes/Cancel over one line of explanation - the Tk GUI's tk_messageBox -type
// yesno, used before anything that throws work away. `subject` rides along so a
// shared instance still knows which row the Yes belongs to.
SheetDialog {
    id: dlg

    property string message: ""
    property string subject: ""

    standardButtons: Dialog.Cancel | Dialog.Yes

    Text {
        objectName: "confirmMessage"
        width: dlg.availableWidth
        text: dlg.message
        color: Theme.textPrimary
        wrapMode: Text.WordWrap
    }
}
