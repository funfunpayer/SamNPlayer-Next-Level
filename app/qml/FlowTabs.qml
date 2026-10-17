import QtQuick
import SamN

// Pill tabs in the Flow look (0.7 bottom tabs, inspector tabs).
Row {
    id: root
    property var model: []
    property int current: 0
    signal picked(int index)
    spacing: 4
    Repeater {
        model: root.model
        delegate: Rectangle {
            required property int index
            required property string modelData
            readonly property bool on: index === root.current
            height: 30; width: label.implicitWidth + 26; radius: 15
            color: on ? Qt.rgba(0, 120 / 255, 1, 0.30) : area.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
            border.color: on ? Theme.cyan : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Text {
                textFormat: Text.PlainText
                id: label
                anchors.centerIn: parent
                text: modelData
                color: on ? Theme.text : Theme.textDim
                font.family: Theme.fontSoft; font.pixelSize: 13
            }
            MouseArea {
                id: area
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.picked(index)
            }
        }
    }
}
