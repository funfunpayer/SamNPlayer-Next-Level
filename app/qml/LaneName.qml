import QtQuick
import SamN

// A lane's name left of the lanes (0.7 v1 name column): a glowing dot in
// the lane colour and the name. Selectable lanes are 0.7's legend chips: a
// click picks the lane for the main curve, Up / Down move to the next one.
Item {
    id: root
    property string name
    property color dot: Theme.textDim
    property bool selectable: false
    property bool active: false
    signal picked()
    signal stepped(int delta)
    width: parent ? parent.width : 108
    activeFocusOnTab: selectable
    Accessible.role: selectable ? Accessible.Button : Accessible.StaticText
    Accessible.name: name
    Accessible.checkable: selectable
    Accessible.checked: active
    Keys.onUpPressed: root.stepped(-1)
    Keys.onDownPressed: root.stepped(1)
    Keys.onSpacePressed: root.picked()
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: 8
        color: Qt.rgba(1, 1, 1, root.active ? 0.07 : (hover.containsMouse ? 0.04 : 0))
        border.color: root.activeFocus ? Theme.cyan : "transparent"
        visible: root.selectable
    }
    Rectangle {
        id: glow
        x: 4; anchors.verticalCenter: parent.verticalCenter
        width: 18; height: 18; radius: 9
        color: root.dot; opacity: root.active ? 0.45 : 0.25
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
        color: root.active ? Theme.text : (root.selectable ? Theme.textDim : Theme.text)
        font.family: Theme.fontSoft; font.pixelSize: 13; font.bold: root.active
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        enabled: root.selectable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.forceActiveFocus(); root.picked() }
    }
}
