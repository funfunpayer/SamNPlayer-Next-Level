import QtQuick
import QtQuick.Window
import SamN

// "Choose a mode": one card per mode, keys 1-4, Esc closes. Fades and scales in.
// Generator and Editor are one mode (Owner 16 Oct, D-137).
// Keyboard as the 0.7 tabs: arrows and Home/End move the focus ring, Tab
// stays inside, Enter or Space picks; focus returns where it was on close.
FocusScope {
    id: root
    property int current: 0
    property bool open: false
    // The card with the focus ring (keyboard cursor; the mouse moves it too).
    property int focusIndex: 0
    property Item returnFocus: null
    signal chosen(int index)
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Choose a mode")

    onOpenChanged: {
        if (open) {
            returnFocus = Window.activeFocusItem
            focusIndex = current
            forceActiveFocus()
        } else {
            focus = false  // a hidden deck must not keep the keys
            if (returnFocus) returnFocus.forceActiveFocus()
            returnFocus = null
        }
    }
    function move(step) { focusIndex = (focusIndex + step + modes.length) % modes.length }
    function pick(i) { chosen(i); open = false }
    Keys.enabled: open
    Keys.onLeftPressed: move(-1)
    Keys.onUpPressed: move(-1)
    Keys.onRightPressed: move(1)
    Keys.onDownPressed: move(1)
    Keys.onTabPressed: move(1)
    Keys.onBacktabPressed: move(-1)
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Home) { focusIndex = 0; event.accepted = true }
        else if (event.key === Qt.Key_End) { focusIndex = modes.length - 1; event.accepted = true }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            pick(focusIndex); event.accepted = true
        }
    }
    readonly property var modes: [
        { name: qsTr("Player"), text: qsTr("Play scripts with video\nand your device"), color: Theme.cyan, icon: "player" },
        { name: qsTr("Generator"), text: qsTr("Create a script from a\nvideo and edit it"), color: Theme.orange, icon: "create" },
        { name: qsTr("Training"), text: qsTr("Guided sessions with\nlive curves"), color: Theme.blue, icon: "training" }
    ]
    // Three modes (D-144, D-145): the Scene Map is a view in the Editor, not a mode.
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }

    // Dim the app behind the deck; a click outside closes it.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1 / 255, 7 / 255, 15 / 255, 0.72)
        MouseArea { anchors.fill: parent; onClicked: root.open = false }
    }
    GlassCard {
        id: deck
        anchors.horizontalCenter: parent.horizontalCenter
        y: 82
        width: Math.min(parent.width - 48, 1000); height: 246
        scale: root.open ? 1 : 0.96
        Behavior on scale { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Text {
            textFormat: Text.PlainText
            x: 22; y: 18
            text: qsTr("CHOOSE A MODE"); color: Theme.textDim
            font.family: Theme.fontSoft; font.pixelSize: 12; font.letterSpacing: 3
        }
        Text {
            textFormat: Text.PlainText
            anchors.right: parent.right; anchors.rightMargin: 22; y: 18
            text: "1–" + root.modes.length + " · ← → · ENTER · ESC"; color: Theme.textMuted; font.pixelSize: 11
        }
        Row {
            x: 18; y: 48
            spacing: 12
            Repeater {
                model: root.modes
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: (deck.width - 36 - (root.modes.length - 1) * 12) / root.modes.length; height: 178; radius: 16
                    color: index === root.current ? Qt.rgba(0, 120 / 255, 1, 0.18) : Qt.rgba(1, 1, 1, cardArea.containsMouse ? 0.05 : 0.02)
                    border.width: index === root.current ? 1.5 : 1
                    border.color: index === root.current ? Theme.cyan : Theme.cardBorder
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.name
                    Accessible.description: modelData.text
                    // Focus ring of the keyboard cursor, outside the card's own border.
                    Rectangle {
                        objectName: "focusRing"
                        anchors.fill: parent; anchors.margins: -4
                        radius: parent.radius + 4
                        color: "transparent"
                        border.width: 2; border.color: Qt.rgba(1, 1, 1, 0.7)
                        visible: root.open && index === root.focusIndex
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                        width: 20; height: 20; radius: 5
                        color: "transparent"; border.color: Theme.cardBorder
                        Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: index + 1; color: Theme.textDim; font.pixelSize: 11 }
                    }
                    Rectangle {
                        id: badge
                        anchors.horizontalCenter: parent.horizontalCenter; y: 18
                        width: 70; height: 70; radius: 35
                        color: "#06182c"; border.width: 2; border.color: modelData.color
                        Icon { anchors.centerIn: parent; name: modelData.icon; color: modelData.color; width: 28; height: 28 }
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.horizontalCenter: parent.horizontalCenter; y: 100
                        text: modelData.name; color: "white"
                        font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.horizontalCenter: parent.horizontalCenter; y: 126
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.text; color: Theme.textDim
                        font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    MouseArea {
                        id: cardArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.focusIndex = index
                        onClicked: root.pick(index)
                    }
                }
            }
        }
    }
}
