import QtQuick
import SamN

// 0-100 position on the video (0.7 pos-gauge): scale, track with fill and
// knob, and the value ("—" when unknown).
Item {
    id: root
    property int value: -1     // < 0: unknown
    width: 54; height: 190
    readonly property real frac: value < 0 ? 0 : Math.max(0, Math.min(100, value)) / 100
    Rectangle {
        anchors.fill: parent; radius: 14
        color: Qt.rgba(2 / 255, 10 / 255, 20 / 255, 0.62); border.color: Theme.cardBorder
    }
    Column {
        x: 6; y: 12; height: parent.height - 44
        spacing: (height - 3 * 12) / 2
        Repeater {
            model: ["100", "50", "0"]
            Text { textFormat: Text.PlainText; required property string modelData; text: modelData; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 9 }
        }
    }
    Rectangle {
        id: track
        x: 34; y: 16; width: 8; height: parent.height - 52; radius: 4
        color: Qt.rgba(1, 1, 1, 0.10)
        Rectangle {
            anchors.bottom: parent.bottom; width: parent.width; radius: 4
            height: parent.height * root.frac
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.orange }
                GradientStop { position: 1; color: Theme.cyan }
            }
            Behavior on height { NumberAnimation { duration: 80 } }
        }
        Rectangle {
            visible: root.value >= 0
            width: 14; height: 14; radius: 7; x: -3
            y: parent.height * (1 - root.frac) - 7
            color: "#dff8ff"; border.color: Theme.blue; border.width: 2
            Behavior on y { NumberAnimation { duration: 80 } }
        }
    }
    Text {
        textFormat: Text.PlainText
        anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 8
        text: root.value < 0 ? "—" : root.value
        color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
    }
}
