import QtQuick
import SamN

// A horizontal slider: cyan→orange fill, glowing knob.
Item {
    id: root
    property real from: 0
    property real to: 1
    property real value: 0
    signal moved(real value)
    implicitWidth: 140; implicitHeight: 20
    readonly property real frac: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0
    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 5; radius: 2.5; color: Qt.rgba(1, 1, 1, 0.10) }
    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width * root.frac; height: 5; radius: 2.5
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.blue }
            GradientStop { position: 1; color: Theme.cyan }
        }
    }
    Rectangle {
        x: parent.width * root.frac - width / 2; anchors.verticalCenter: parent.verticalCenter
        width: 14; height: 14; radius: 7; color: "#dff8ff"; border.color: Theme.blue; border.width: 3
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function set(mx) { const v = root.from + Math.max(0, Math.min(1, mx / width)) * (root.to - root.from); root.value = v; root.moved(v) }
        onPressed: (m) => set(m.x)
        onPositionChanged: (m) => { if (pressed) set(m.x) }
    }
}
