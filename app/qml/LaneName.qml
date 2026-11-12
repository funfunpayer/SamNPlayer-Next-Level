import QtQuick
import SamN

// A lane's name left of the lanes (0.7 v1 name column): a glowing dot in
// the lane colour and the name. Selectable lanes are 0.7's legend chips: a
// click picks the lane for the main curve, Up / Down move to the next one.
// chip=true: compact horizontal toolbar chip (0.7 pb-axis-chip row).
Item {
    id: root
    property string name
    property color dot: Theme.textDim
    property bool selectable: false
    property bool active: false
    property bool chip: false
    signal picked()
    signal stepped(int delta)
    implicitWidth: chip ? dotGlow.width + nameText.implicitWidth + (selectable ? 20 : 14) : (parent ? parent.width : 108)
    width: chip ? implicitWidth : (parent ? parent.width : 108)
    height: chip ? 28 : implicitHeight
    activeFocusOnTab: selectable
    Accessible.role: selectable ? Accessible.Button : Accessible.StaticText
    Accessible.name: name
    Accessible.checkable: selectable
    Accessible.checked: active
    Keys.onUpPressed: root.stepped(-1)
    Keys.onDownPressed: root.stepped(1)
    Keys.onLeftPressed: root.stepped(-1)
    Keys.onRightPressed: root.stepped(1)
    Keys.onSpacePressed: root.picked()
    // Home/End have no Keys.on*Pressed handlers in Qt 6 — route via onPressed.
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home) { root.stepped(-1000); event.accepted = true }
        else if (event.key === Qt.Key_End) { root.stepped(1000); event.accepted = true }
    }
    Rectangle {
        anchors.fill: parent; anchors.margins: chip ? 0 : 1
        radius: chip ? 14 : 8
        color: Qt.rgba(1, 1, 1, root.active ? (chip ? 0.1 : 0.07) : (hover.containsMouse ? 0.04 : 0))
        border.color: root.active ? Qt.rgba(root.dot.r, root.dot.g, root.dot.b, chip ? 0.55 : 0.35)
                     : (root.activeFocus ? Theme.cyan : "transparent")
        border.width: chip && (root.active || root.activeFocus) ? 1 : (root.activeFocus && !chip ? 1 : 0)
        visible: root.selectable || root.chip
    }
    Rectangle {
        id: dotGlow
        x: chip ? 8 : 4; anchors.verticalCenter: parent.verticalCenter
        width: chip ? 14 : 18; height: chip ? 14 : 18; radius: chip ? 7 : 9
        color: root.dot; opacity: root.active ? 0.45 : 0.25
        visible: root.name !== ""
    }
    Rectangle {
        anchors.centerIn: dotGlow
        width: chip ? 8 : 10; height: chip ? 8 : 10; radius: chip ? 4 : 5
        color: root.dot
        visible: root.name !== ""
    }
    Text {
        id: nameText
        textFormat: Text.PlainText
        x: chip ? 26 : 28; anchors.verticalCenter: parent.verticalCenter
        width: chip ? implicitWidth : parent.width - 30
        elide: chip ? Text.ElideNone : Text.ElideRight
        text: root.name
        color: root.active ? Theme.text : (root.selectable ? Theme.textDim : Theme.text)
        font.family: Theme.fontSoft; font.pixelSize: chip ? 12 : 13; font.bold: root.active
    }
    MouseArea {
        id: hover
        z: 10
        anchors.fill: parent
        enabled: root.selectable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // Pick first; focus second — forceActiveFocus can fail offscreen.
        onClicked: { root.picked(); root.forceActiveFocus() }
    }
}
