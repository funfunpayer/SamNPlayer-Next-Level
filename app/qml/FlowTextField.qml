import QtQuick
import SamN

// A one-line text field in the Flow look.
Rectangle {
    id: root
    property alias text: input.text
    property string placeholder
    property int maxLength: 80
    signal accepted()
    implicitWidth: 200; implicitHeight: 32
    radius: 10
    color: Qt.rgba(1, 1, 1, 0.03)
    border.color: input.activeFocus ? Theme.cyan : Theme.cardBorder
    function focusField() { input.forceActiveFocus() }
    TextInput {
        id: input
        anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.text; selectionColor: Theme.blue
        font.family: Theme.fontSoft; font.pixelSize: 13
        maximumLength: root.maxLength
        clip: true
        onAccepted: root.accepted()
    }
    Text {
        anchors.fill: input
        verticalAlignment: Text.AlignVCenter
        visible: input.text === "" && !input.activeFocus
        text: root.placeholder
        color: Theme.textMuted; font: input.font
    }
}
