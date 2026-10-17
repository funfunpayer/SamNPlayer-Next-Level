import QtQuick
import SamN

// Brand, the Mode pill (opens the Mode deck) and the window-level actions.
Item {
    id: root
    property string modeName: "Player"
    property string modeIcon: "player"
    // The app's one device connection (Main.qml); null hides the chip.
    property DeviceController device: null
    signal modeDeckRequested()
    // Open a video or script (0.7 top-bar folder button, Ctrl+O).
    signal openRequested()
    // The chip was clicked: show the device controls.
    signal deviceRequested()
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

    // Right: device chip, Open, version (0.7 top bar).
    Row {
        anchors.right: parent.right; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10
        Rectangle {
            id: chip
            objectName: "deviceChip"
            visible: root.device !== null
            readonly property var dev: root.device
            readonly property string stateName: dev ? dev.state : "disconnected"
            readonly property string label: !dev ? ""
                : stateName === "connected" ? (dev.mock ? qsTr("Mock device") : (dev.name !== "" ? dev.name : qsTr("Connected")))
                  + (dev.batteryOk ? " · " + dev.batteryPercent + "%" : "")
                : stateName === "connecting" ? qsTr("Searching…") : qsTr("Not connected")
            anchors.verticalCenter: parent.verticalCenter
            height: 36; radius: 18
            width: chipRow.width + 8
            // Opaque: the light ribbons run behind the top-right corner.
            color: chipArea.containsMouse ? "#12304f" : Qt.rgba(6 / 255, 22 / 255, 42 / 255, 0.92)
            border.color: Theme.cardBorder
            Row {
                id: chipRow
                x: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Rectangle {
                    objectName: "deviceLed"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 9; height: 9; radius: 4.5
                    color: chip.stateName === "connected" ? "#3ddc84" : chip.stateName === "connecting" ? Theme.orange : "#5d6b7a"
                    SequentialAnimation on opacity {
                        running: chip.stateName === "connecting"; loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { from: 1; to: 0.3; duration: 500 }
                        NumberAnimation { from: 0.3; to: 1; duration: 500 }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    objectName: "deviceChipLabel"
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.label; color: Theme.text
                    font.family: Theme.fontSoft; font.pixelSize: 12
                    elide: Text.ElideRight; width: Math.min(implicitWidth, 180)
                }
                FlowButton {
                    id: chipButton
                    objectName: "deviceChipButton"
                    anchors.verticalCenter: parent.verticalCenter
                    height: 26
                    text: chip.stateName === "connected" ? qsTr("Disconnect") : chip.stateName === "connecting" ? qsTr("Cancel") : qsTr("Connect")
                    onClicked: {
                        if (chip.stateName === "disconnected") {
                            // The saved choice of Inspector → Devices (0.7 deviceTransport).
                            const kind = chip.dev.savedKind()
                            chip.dev.connectDevice(kind, chip.dev.savedTarget(kind))
                        } else {
                            chip.dev.disconnectDevice()
                        }
                    }
                }
            }
            MouseArea {
                id: chipArea
                // The chip without its button: show the device controls.
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: chipRow.x + chipButton.x - 4
                hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: root.deviceRequested()
            }
            ToolTipText {
                shown: chipArea.containsMouse
                text: chip.dev && chip.stateName === "connected"
                      ? [chip.dev.address, chip.dev.batteryOk ? qsTr("Battery %1%").arg(chip.dev.batteryPercent) : ""].filter(t => t !== "").join(" · ")
                      : qsTr("Device: click for the connection settings")
            }
        }
        IconButton {
            objectName: "topOpenButton"
            anchors.verticalCenter: parent.verticalCenter
            width: 36; height: 36
            icon: "folder"; tip: qsTr("Open a video or script (Ctrl+O)")
            color: Qt.rgba(6 / 255, 22 / 255, 42 / 255, 0.92)
            onClicked: root.openRequested()
        }
        Text {
            textFormat: Text.PlainText
            objectName: "versionLabel"
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.application.version !== "" ? "v" + Qt.application.version : ""
            style: Text.Outline; styleColor: Qt.rgba(0, 0, 0, 0.5)
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
    }
}
