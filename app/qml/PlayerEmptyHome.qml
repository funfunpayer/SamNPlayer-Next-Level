import QtQuick
import SamN

// Empty state (Owner 15 Oct, 0.8 #550): one real round Play button,
// "Open…" under it, centred. No wordmark or stage art. Soft bloom and
// a calm settle-in so the home feels alive without clutter.
Item {
    id: root
    property bool empty
    signal openRequested()

    anchors.fill: parent
    visible: empty
    opacity: empty ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic } }

    Column {
        id: emptyState
        anchors.centerIn: parent
        spacing: 18
        scale: root.empty ? 1 : 0.96
        Behavior on scale { NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic } }

        // Soft round bloom behind the Play only (Owner 26 Oct: the old
        // wide oval looked like a dark smear behind the button). Layout
        // size stays the Play; the bloom is larger and does not push Open….
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: emptyPlay.width; height: emptyPlay.height
            Rectangle {
                id: emptyBloom
                property real breathe: 0.85
                anchors.centerIn: parent
                width: emptyPlay.width * 2.2; height: width; radius: width / 2
                opacity: breathe
                z: -1
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(63 / 255, 208 / 255, 1, 0.14) }
                    GradientStop { position: 0.5; color: Qt.rgba(1, 138 / 255, 35 / 255, 0.07) }
                    GradientStop { position: 1; color: "transparent" }
                }
                SequentialAnimation on breathe {
                    running: root.empty && Qt.application.state === Qt.ApplicationActive && !Theme.reducedMotion
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.65; to: 1.0; duration: 3800; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1.0; to: 0.65; duration: 3800; easing.type: Easing.InOutSine }
                }
            }
            PlayButton {
                id: emptyPlay
                objectName: "emptyPlay"
                anchors.centerIn: parent
                // Grows with the stage, 96..140 px (Owner 26 Oct: 168 px was
                // too big on small windows and at 150 % scaling).
                width: Math.round(Math.max(96, Math.min(140, Math.min(root.width, root.height) * 0.2)))
                height: width
                onClicked: root.openRequested()
            }
        }
        // Match 0.8 `.pb-empty-open` (letter-spacing + soft cyan glow).
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: emptyOpenLabel.implicitWidth
            height: emptyOpenLabel.implicitHeight
            Text {
                anchors.centerIn: parent
                textFormat: Text.PlainText
                text: emptyOpenLabel.text
                color: Qt.rgba(63 / 255, 208 / 255, 1, 0.45)
                font: emptyOpenLabel.font
                opacity: emptyOpen.containsMouse ? 0.85 : 0.55
                scale: 1.10
                z: -1
                Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
            }
            Text {
                id: emptyOpenLabel
                textFormat: Text.PlainText
                text: qsTr("Open…")
                color: emptyOpen.containsMouse ? "white" : Theme.text
                font.family: Theme.font; font.pixelSize: 20; font.weight: Font.DemiBold
                font.letterSpacing: 0.8
                Behavior on color { ColorAnimation { duration: Theme.animFast } }
            }
            MouseArea {
                id: emptyOpen
                anchors.fill: parent; anchors.margins: -8
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.openRequested()
            }
        }
    }
}
