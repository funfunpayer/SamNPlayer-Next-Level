import QtQuick
import SamN

Row {
    property string name
    property bool on
    property int level
    signal toggled()
    signal levelPicked(int v)
    spacing: 8; height: 30
    FlowButton {
        width: 54; implicitHeight: 28; text: parent.on ? qsTr("On") : qsTr("Off"); checked: parent.on
        onClicked: parent.toggled()
    }
    Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 62; text: parent.name; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
    FlowSlider {
        anchors.verticalCenter: parent.verticalCenter; width: 92
        enabled: parent.on; opacity: enabled ? 1 : 0.45
        from: 0; to: 100; value: parent.level
        onMoved: (v) => parent.levelPicked(Math.round(v / 5) * 5)
    }
    Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: parent.level + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
}
