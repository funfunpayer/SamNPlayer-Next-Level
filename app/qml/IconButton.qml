import QtQuick
import SamN

// A round icon button (glyph text) in the Flow look.
Rectangle {
    id: root
    property alias glyph: label.text
    // A Flow icon name (preferred over glyph).
    property string icon
    property string tip
    signal clicked()
    width: 44; height: 44; radius: 22
    color: area.containsMouse ? Qt.rgba(1, 1, 1, 0.09) : "transparent"
    border.color: area.containsMouse ? Qt.rgba(63 / 255, 208 / 255, 1, 0.45) : Theme.cardBorder
    border.width: 1
    scale: area.pressed ? 0.92 : (area.containsMouse ? 1.06 : 1.0)
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
    Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
    Text { textFormat: Text.PlainText; id: label; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 17; visible: root.icon === "" }
    Icon {
        anchors.centerIn: parent
        name: root.icon
        width: Math.round(root.height * 0.46); height: width
        visible: root.icon !== ""
        color: area.containsMouse ? Theme.cyanSoft : Theme.text
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }
    MouseArea {
        id: area
        anchors.fill: parent; hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
