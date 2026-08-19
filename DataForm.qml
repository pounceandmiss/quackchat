pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// An XEP-0004 data form, drawn from whatever the server asked for: one control
// per row of `fields`, picked by the row's type. The Tk GUI's regform, which
// this follows, renders the same set - a form is the server's questions and
// there is nothing to decide here beyond which control answers each one.
//
// Answers go straight back into the model as they are typed (`setValue(row,
// value)`), so the model holds the form's state and this holds none.
// Registration is the first user; a MUC's config form is the same wire type
// and the same rows.
ColumnLayout {
    id: form

    // A RegistrationController, or any model with the same roles and a
    // setValue(row, value) to write an answer back through.
    property var fields: null

    spacing: 12

    Repeater {
        model: form.fields

        delegate: ColumnLayout {
            id: field

            required property int index
            required property string type
            required property string label
            required property bool required
            required property string value
            required property var values
            required property var options
            required property string mediaSource
            required property bool hasMedia

            // Which of the controls below answers this field. Several types
            // share one, and an unknown type is a line to type in: naming it
            // once beats five bindings each spelling out what it is not.
            readonly property string control: {
                switch (field.type) {
                case "fixed": return "prose"
                case "boolean": return "tick"
                case "text-multi": return "block"
                case "list-single": return "choice"
                case "list-multi": return "ticks"
                default: return "line"
                }
            }

            Layout.fillWidth: true
            spacing: 4

            // A tick carries its own label and a fixed field is a line of the
            // server's prose, so neither wants one above it.
            Label {
                Layout.fillWidth: true
                visible: field.control !== "prose" && field.control !== "tick"
                text: field.required ? field.label + " *" : field.label
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: field.control === "prose"
                text: field.value
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            TextField {
                id: line
                Layout.fillWidth: true
                objectName: "formLine" + field.index
                visible: field.control === "line"
                text: field.value
                echoMode: field.type === "text-private" ? TextInput.Password
                                                        : TextInput.Normal
                inputMethodHints: field.type === "jid-single"
                                  ? Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                                  : Qt.ImhNone
                onTextEdited: form.fields.setValue(field.index, line.text)
            }

            CheckBox {
                id: tick
                objectName: "formTick" + field.index
                visible: field.control === "tick"
                text: field.label
                checked: field.value === "1" || field.value === "true"
                // The wire spells a boolean 1/0, as XEP-0004 does.
                onToggled: form.fields.setValue(field.index, tick.checked ? "1" : "0")
            }

            TextArea {
                id: block
                Layout.fillWidth: true
                Layout.minimumHeight: 72
                objectName: "formBlock" + field.index
                visible: field.control === "block"
                wrapMode: TextEdit.Wrap
                text: field.values.join("\n")
                // TextEdit has no textEdited, so this fires for the model's own
                // writes too, and once per row at build. Only the shown control
                // answers; what it writes back is what it was just given.
                onTextChanged: if (block.visible)
                                   form.fields.setValue(field.index,
                                                        block.text.split("\n"))
            }

            ComboBox {
                id: choice
                Layout.fillWidth: true
                objectName: "formChoice" + field.index
                visible: field.control === "choice"
                model: field.options
                textRole: "label"
                valueRole: "value"
                currentIndex: {
                    for (let i = 0; i < field.options.length; ++i) {
                        if (field.options[i].value === field.value)
                            return i
                    }
                    return -1
                }
                onActivated: form.fields.setValue(field.index, choice.currentValue)
            }

            // No multi-select combo in Controls, and a column of ticks is what
            // a form of a handful of options wants anyway.
            ColumnLayout {
                Layout.fillWidth: true
                visible: field.control === "ticks"
                spacing: 0

                Repeater {
                    model: field.control === "ticks" ? field.options : []

                    delegate: CheckBox {
                        id: option
                        required property var modelData
                        text: option.modelData.label
                        checked: field.values.indexOf(option.modelData.value) >= 0
                        onToggled: {
                            const picked = field.values.filter(
                                (v) => v !== option.modelData.value)
                            if (option.checked)
                                picked.push(option.modelData.value)
                            form.fields.setValue(field.index, picked)
                        }
                    }
                }
            }

            // A CAPTCHA (XEP-0158): the bytes are a round trip of their own, so
            // the field is drawn before there is a picture to put under it.
            Image {
                objectName: "formMedia" + field.index
                Layout.topMargin: 4
                Layout.maximumWidth: form.width
                visible: field.mediaSource !== ""
                source: field.mediaSource
                fillMode: Image.PreserveAspectFit
            }

            Caption {
                Layout.fillWidth: true
                visible: field.hasMedia && field.mediaSource === ""
                text: "The picture for this question didn't arrive."
                wrapMode: Text.WordWrap
            }
        }
    }
}
