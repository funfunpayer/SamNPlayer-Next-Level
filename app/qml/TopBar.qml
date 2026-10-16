import QtQuick
import SamN

// Brand, the Mode pill (opens the Mode deck) and the window-level actions.
Item {
    id: root
    property string modeName: "Player"
    property url modeIcon
    signal modeDeckRequested()
    height: 86

    Row {
        id: brand
        anchors.left: parent.left; anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14
        Image {
            source: "assets/logo.png"
            width: 58; height: 58
            sourceSize: Qt.size(116, 116)
            smooth: true; mipmap: true
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            Text {
                text: "SamNPlayer"
                font.family: Theme.font; font.pixelSize: 34; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
                color: Theme.orange
            }
            Text {
                text: qsTr("GENERATE  ·  PLAY  ·  TRAIN")
                font.family: Theme.fontSoft; font.pixelSize: 11; font.letterSpacing: 3.7
                color: "#b9cde0"
            }
        }
    }

    // Mode pill: glowing, opens the deck (or press 1-5 / M).
    Rectangle {
        id: pill
        anchors.centerIn: parent
        width: 228; height: 58; radius: height / 2
        border.width: 1.5
        border.color: hover.containsMouse ? Theme.cyan : Qt.rgba(0, 217 / 255, 244 / 255, 0.7)
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(0, 120 / 255, 1, 0.40) }
            GradientStop { position: 1; color: Qt.rgba(0, 70 / 255, 160 / 255, 0.35) }
        }
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Rectangle {
                width: 40; height: 40; radius: 20
                color: "#05223f"; border.color: Theme.cyan; border.width: 1.5
                Text { anchors.centerIn: parent; text: "▶"; color: Theme.cyan; font.pixelSize: 16 }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { text: qsTr("MODE"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 10; font.letterSpacing: 2.5 }
                Text { text: root.modeName; color: "white"; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold }
            }
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: "⌄"; color: Theme.text; font.pixelSize: 18
        }
        MouseArea {
            id: hover
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.modeDeckRequested()
        }
    }

    Text {
        anchors.right: parent.right; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        text: "v2.0 dev"
        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
    }
}
