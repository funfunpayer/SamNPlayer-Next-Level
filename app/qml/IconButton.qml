import QtQuick
import SamN

// A round icon button (glyph text) in the Flow look.
Rectangle {
    id: root
    property alias glyph: label.text
    property string tip
    signal clicked()
    width: 44; height: 44; radius: 22
    color: area.containsMouse ? Qt.rgba(1, 1, 1, 0.07) : "transparent"
    border.color: Theme.cardBorder; border.width: 1
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Text { id: label; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 17 }
    MouseArea {
        id: area
        anchors.fill: parent; hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
