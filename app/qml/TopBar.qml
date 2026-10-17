import QtQuick
import SamN

// Brand, the Mode pill (opens the Mode deck) and the window-level actions.
Item {
    id: root
    property string modeName: "Player"
    property string modeIcon: "player"
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
                textFormat: Text.PlainText
                text: "SamNPlayer"
                font.family: Theme.font; font.pixelSize: 34; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
                color: Theme.orange
            }
            Text {
                textFormat: Text.PlainText
                text: qsTr("GENERATE  ·  PLAY  ·  TRAIN")
                font.family: Theme.fontSoft; font.pixelSize: 11; font.letterSpacing: 3.7
                color: "#b9cde0"
            }
        }
    }

    // Soft breathing glow around the Mode pill (0.8 #550).
    Rectangle {
        anchors.centerIn: pill
        width: pill.width + 10; height: pill.height + 10; radius: height / 2
        color: "transparent"; border.width: 4
        border.color: Qt.rgba(0, 217 / 255, 244 / 255, 0.35)
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.25; to: 0.9; duration: 1800; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.9; to: 0.25; duration: 1800; easing.type: Easing.InOutSine }
        }
    }
    // Mode pill: glowing, opens the deck (or press M, then a number).
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
                Icon { anchors.centerIn: parent; name: root.modeIcon; color: Theme.cyan; width: 18; height: 18 }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { textFormat: Text.PlainText; text: qsTr("MODE"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 10; font.letterSpacing: 2.5 }
                Text { textFormat: Text.PlainText; text: root.modeName; color: "white"; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold }
            }
        }
        Text {
            textFormat: Text.PlainText
            anchors.right: parent.right; anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            text: ""; color: Theme.text; font.pixelSize: 18
            Icon { anchors.centerIn: parent; name: "chevronDown"; color: Theme.text; width: 18; height: 18 }
        }
        MouseArea {
            id: hover
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.modeDeckRequested()
        }
    }

    Text {
        textFormat: Text.PlainText
        anchors.right: parent.right; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        text: "v2.0 dev"
        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
    }
}
