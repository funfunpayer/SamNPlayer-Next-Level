import QtQuick
import SamN

// A pill button in the Flow look. "primary" is the cyan call to action.
Rectangle {
    id: root
    property alias text: label.text
    property bool primary: false
    property bool checked: false
    property bool enabledLook: enabled
    signal clicked()
    implicitWidth: label.implicitWidth + 32
    implicitHeight: 36
    radius: height / 2
    opacity: enabled ? 1 : 0.45
    color: primary ? (area.containsMouse ? "#25d6ff" : Theme.cyan)
                   : checked ? Qt.rgba(0, 120 / 255, 1, 0.35)
                   : area.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03)
    border.width: 1
    border.color: primary ? "transparent" : checked ? Theme.cyan : Theme.cardBorder
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    scale: area.pressed ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: Theme.animFast } }
    Text {
        id: label
        anchors.centerIn: parent
        color: root.primary ? "#02101f" : Theme.text
        font.family: Theme.fontSoft; font.pixelSize: 14
        font.weight: root.primary ? Font.DemiBold : Font.Normal
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
