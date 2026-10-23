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
            id: tab
            required property int index
            required property string modelData
            readonly property bool on: index === root.current
            height: 30; width: label.implicitWidth + 26; radius: 15
            color: tab.on ? Qt.rgba(0, 120 / 255, 1, 0.32) : area.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
            border.color: tab.on ? Theme.cyan : area.containsMouse ? Qt.rgba(90 / 255, 170 / 255, 255 / 255, 0.35) : "transparent"
            border.width: 1
            scale: area.pressed ? 0.96 : (area.containsMouse && !tab.on ? 1.04 : 1.0)
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
            Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
            // Soft cyan wash under the current pill.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 6; height: parent.height + 4; radius: height / 2; z: -1
                visible: tab.on
                color: Qt.rgba(0, 217 / 255, 244 / 255, 0.12)
                SequentialAnimation on opacity {
                    // Bind to tab.on — parent here is this wash rect, not the pill.
                    running: tab.on && Qt.application.state === Qt.ApplicationActive
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.35; to: 0.85; duration: 1600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0.35; duration: 1600; easing.type: Easing.InOutSine }
                }
            }
            Text {
                textFormat: Text.PlainText
                id: label
                anchors.centerIn: parent
                text: modelData
                color: tab.on ? Theme.text : Theme.textDim
                font.family: Theme.fontSoft; font.pixelSize: 13
                font.weight: tab.on ? Font.DemiBold : Font.Normal
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }
            MouseArea {
                id: area
                anchors.fill: parent; hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.picked(tab.index)
            }
        }
    }
}
