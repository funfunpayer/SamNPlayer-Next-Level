import QtQuick
import SamN

Item {
    id: ch
    property string name
    property color dot
    property int value
    property bool hasScript: false
    width: parent ? parent.width : 260; height: 26
    Rectangle {
        y: 9; width: 8; height: 8; radius: 4; color: ch.dot
        SequentialAnimation on opacity {
            running: ch.value > 0 && Qt.application.state === Qt.ApplicationActive && !Theme.reducedMotion
            loops: Animation.Infinite
            NumberAnimation { from: 0.55; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.55; duration: 900; easing.type: Easing.InOutSine }
        }
    }
    Text { textFormat: Text.PlainText; x: 16; anchors.verticalCenter: parent.verticalCenter; text: ch.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
    Rectangle {
        x: 100; anchors.verticalCenter: parent.verticalCenter
        width: parent.width - 150; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
        Rectangle {
            id: fill
            width: parent.width * ch.value / 100; height: parent.height; radius: 3; color: ch.dot
            Behavior on width { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 2; height: parent.height + 4; radius: 4; z: -1
                color: ch.dot; opacity: ch.value > 0 ? 0.28 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
            }
        }
    }
    Text {
        textFormat: Text.PlainText
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        text: ch.hasScript ? ch.value : "–"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
    }
}
