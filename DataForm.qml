pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Quack

// An XEP-0004 data form, drawn from whatever the server asked for: one control
// per row of `fields`, picked by the row's type. The Tk GUI's regform, which
// this follows, renders the same set - a form is the server's questions and
// there is nothing here to decide beyond which control answers each one.
//
// Answers go straight back into the model as they are typed (`setValue(row,
// value)`), so the model is what holds the form's state and this holds none.
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

            readonly property bool isFixed: field.type === "fixed"

            Layout.fillWidth: true
            spacing: 4

            // A boolean answers in its own box, and a fixed field is a line of
            // the server's prose, so neither wants a label above it.
            Label {
                Layout.fillWidth: true
                visible: !field.isFixed && field.type !== "boolean"
                text: field.required ? field.label + " *" : field.label
                color: Theme.textDim
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                visible: field.isFixed
                text: field.value
                color: Theme.textPrimary
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            TextField {
                id: line
                Layout.fillWidth: true
                objectName: "formLine" + field.index
                visible: !field.isFixed && field.type !== "boolean"
                       && field.type !== "text-multi" && field.type !== "list-single"
                       && field.type !== "list-multi"
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
                visible: field.type === "boolean"
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
                visible: field.type === "text-multi"
                wrapMode: TextEdit.Wrap
                text: field.values.join("\n")
                // TextEdit has no textEdited, so this also fires when the model
                // pushes a value in - and once at build, for every row, since
                // each row builds every control and shows one. Only the shown
                // one answers; writing back what just arrived is then a no-op,
                // the text it sets being the text already there.
                onTextChanged: if (block.visible)
                                   form.fields.setValue(field.index,
                                                        block.text.split("\n"))
            }

            ComboBox {
                id: choice
                Layout.fillWidth: true
                objectName: "formChoice" + field.index
                visible: field.type === "list-single"
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

            // No multi-select combo in Controls, and a list of ticks is what a
            // form of a handful of options wants anyway.
            ColumnLayout {
                Layout.fillWidth: true
                visible: field.type === "list-multi"
                spacing: 0

                Repeater {
                    model: field.type === "list-multi" ? field.options : []

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

            // A CAPTCHA (XEP-0158): the bytes come as a second round trip, so
            // the field is drawn before there is a picture to put in it.
            Image {
                objectName: "formMedia" + field.index
                Layout.topMargin: 4
                visible: field.hasMedia && field.mediaSource !== ""
                source: field.mediaSource
                fillMode: Image.PreserveAspectFit
                Layout.maximumWidth: form.width
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
