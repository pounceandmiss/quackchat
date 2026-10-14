import QtQuick
import Quack

// The bar across the top of a page: a surface strip with a hairline under it,
// holding whatever the page puts in it. 60px unless the page says otherwise -
// a header that stacks two rows sets its own implicitHeight.
Rectangle {
    default property alias content: body.data

    implicitHeight: 60
    color: Theme.surface

    Item {
        id: body
        anchors.fill: parent
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: Theme.hairline
    }
}
