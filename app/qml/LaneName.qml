import QtQuick
import SamN

// A lane's name left of the lanes (0.7 v1 name column): a glowing dot in
// the lane colour and the name.
Item {
    id: root
    property string name
    property color dot: Theme.textDim
    width: parent ? parent.width : 108
    Rectangle {
        id: glow
        x: 4; anchors.verticalCenter: parent.verticalCenter
        width: 18; height: 18; radius: 9
        color: root.dot; opacity: 0.25
        visible: root.name !== ""
    }
    Rectangle {
        anchors.centerIn: glow
        width: 10; height: 10; radius: 5
        color: root.dot
        visible: root.name !== ""
    }
    Text {
        textFormat: Text.PlainText
        x: 28; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 30; elide: Text.ElideRight
        text: root.name
        color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
    }
}
