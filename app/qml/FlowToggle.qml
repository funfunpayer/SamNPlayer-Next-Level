import QtQuick
import SamN

// An on/off switch in the Flow look.
Rectangle {
    id: root
    property bool checked: false
    signal toggled(bool checked)
    implicitWidth: 42; implicitHeight: 22
    radius: height / 2
    color: checked ? Theme.blue : Qt.rgba(1, 1, 1, 0.12)
    opacity: enabled ? 1 : 0.45
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Rectangle {
        y: 3; x: root.checked ? root.width - width - 3 : 3
        width: root.height - 6; height: width; radius: width / 2; color: "#eaf6ff"
        Behavior on x { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.checked)
    }
}
