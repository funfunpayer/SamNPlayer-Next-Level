import QtQuick
import SamN

// "Choose a mode": five cards, keys 1-5, Esc closes. Fades and scales in.
Item {
    id: root
    property int current: 0
    property bool open: false
    signal chosen(int index)
    readonly property var modes: [
        { name: qsTr("Player"), text: qsTr("Play scripts with video\nand your device"), color: Theme.cyan, glyph: "▶" },
        { name: qsTr("Editor"), text: qsTr("Edit points, axes and\ncurves"), color: "#a07cf0", glyph: "✎" },
        { name: qsTr("Generator"), text: qsTr("Create a script from a\nvideo"), color: Theme.orange, glyph: "✦" },
        { name: qsTr("Training"), text: qsTr("Guided sessions with\nlive curves"), color: Theme.blue, glyph: "∿" },
        { name: qsTr("Scene Map"), text: qsTr("Scenes, chapters and\nrhythm"), color: "#2fd6a8", glyph: "▦" }
    ]
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
            x: 22; y: 18
            text: qsTr("CHOOSE A MODE"); color: Theme.textDim
            font.family: Theme.fontSoft; font.pixelSize: 12; font.letterSpacing: 3
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 22; y: 18
            text: "1–5 · ESC"; color: Theme.textMuted; font.pixelSize: 11
        }
        Row {
            x: 18; y: 48
            spacing: 12
            Repeater {
                model: root.modes
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: (deck.width - 36 - 4 * 12) / 5; height: 178; radius: 16
                    color: index === root.current ? Qt.rgba(0, 120 / 255, 1, 0.18) : Qt.rgba(1, 1, 1, cardArea.containsMouse ? 0.05 : 0.02)
                    border.width: index === root.current ? 1.5 : 1
                    border.color: index === root.current ? Theme.cyan : Theme.cardBorder
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Rectangle {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 10
                        width: 20; height: 20; radius: 5
                        color: "transparent"; border.color: Theme.cardBorder
                        Text { anchors.centerIn: parent; text: index + 1; color: Theme.textDim; font.pixelSize: 11 }
                    }
                    Rectangle {
                        id: badge
                        anchors.horizontalCenter: parent.horizontalCenter; y: 18
                        width: 70; height: 70; radius: 35
                        color: "#06182c"; border.width: 2; border.color: modelData.color
                        Text { anchors.centerIn: parent; text: modelData.glyph; color: modelData.color; font.pixelSize: 26 }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 100
                        text: modelData.name; color: "white"
                        font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter; y: 126
                        horizontalAlignment: Text.AlignHCenter
                        text: modelData.text; color: Theme.textDim
                        font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    MouseArea {
                        id: cardArea
                        anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { root.chosen(index); root.open = false }
                    }
                }
            }
        }
    }
}
