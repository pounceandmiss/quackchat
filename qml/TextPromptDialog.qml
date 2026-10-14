import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// One line of text, asked for and handed back: the Tk GUI's InputDialog, which
// is how it renames a contact and retitles a bookmark. Callers set `prompt` and
// `value` before open() and read the answer off `submitted`, so one instance
// serves every such question on a page.
SheetDialog {
    id: dlg

    property string prompt: ""
    property string value: ""
    // Carried, not read, by this dialog: what the caller was asking about, so
    // an answer arriving later can be applied to the right row.
    property string subject: ""

    signal submitted(string text)

    standardButtons: Dialog.Cancel | Dialog.Ok

    onAboutToShow: {
        field.text = dlg.value
        field.selectAll()
        field.forceActiveFocus()
    }
    onAccepted: dlg.submitted(field.text.trim())

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            Layout.fillWidth: true
            text: dlg.prompt
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        TextField {
            id: field
            objectName: "promptField"
            Layout.fillWidth: true
            onAccepted: dlg.accept()
        }
    }
}
