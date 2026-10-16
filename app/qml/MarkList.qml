import QtQuick
import SamN

// A list of marks (chapters, bookmarks, O-markers) with Seek, Rename and
// Remove per row (0.7 Tools lists). Rename edits the name in place.
ListView {
    id: root
    property bool canRename: true
    property bool canSeek: true
    property string emptyText
    signal seek(int index)
    signal rename(int index, string name)
    signal remove(int index)
    clip: true
    spacing: 4
    boundsBehavior: Flickable.StopAtBounds
    Text {
        visible: root.count === 0
        text: root.emptyText
        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
    }
    delegate: Rectangle {
        id: row
        required property int index
        required property var modelData
        property bool editing: false
        width: root.width; height: 34; radius: 10
        color: hover.hovered ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(1, 1, 1, 0.02)
        HoverHandler { id: hover }
        Text {
            visible: !row.editing
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
            anchors.right: buttons.left; anchors.rightMargin: 8
            text: row.modelData.label; elide: Text.ElideRight
            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
        }
        FlowTextField {
            id: nameField
            visible: row.editing
            anchors.left: parent.left; anchors.leftMargin: 6; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - buttons.width - 20; height: 28
            onAccepted: { root.rename(row.index, text); row.editing = false }
        }
        Row {
            id: buttons
            anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            FlowButton { visible: root.canSeek && !row.editing; implicitHeight: 26; text: qsTr("Seek"); onClicked: root.seek(row.index) }
            FlowButton {
                visible: root.canRename; implicitHeight: 26
                text: row.editing ? qsTr("Save") : qsTr("Rename")
                onClicked: {
                    if (row.editing) { root.rename(row.index, nameField.text); row.editing = false }
                    else { nameField.text = row.modelData.name; row.editing = true; nameField.focusField() }
                }
            }
            FlowButton { implicitHeight: 26; text: qsTr("Remove"); onClicked: root.remove(row.index) }
        }
    }
}
