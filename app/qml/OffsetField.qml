import QtQuick
import SamN

// Script offset: − value + (steps of 50 ms), saved per script by the controller.
Row {
    id: root
    property int value: 0
    signal changed(int value)
    spacing: 6
    IconButton { width: 32; height: 32; icon: "minus"; onClicked: root.changed(root.value - 50) }
    Rectangle {
        width: 92; height: 32; radius: 10
        color: Qt.rgba(1, 1, 1, 0.03); border.color: Theme.cardBorder
        Text {
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: (root.value > 0 ? "+" : "") + root.value + " ms"
            color: root.value === 0 ? Theme.textDim : Theme.text
            font.family: Theme.fontSoft; font.pixelSize: 13
        }
    }
    IconButton { width: 32; height: 32; icon: "plus"; onClicked: root.changed(root.value + 50) }
}
