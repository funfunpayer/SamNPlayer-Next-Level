import QtQuick
import SamN

// The Inspector of the Player (0.7 v1): tabs Devices · Audio · Playback ·
// View. Functions not built yet say which phase brings them; nothing pretends.
GlassCard {
    id: root
    required property PlayerController ctrl
    property bool collapsed: false
    property alias tab: tabs.current
    width: collapsed ? 44 : 320
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    component Card: Rectangle {
        default property alias content: body.data
        property string title
        width: parent ? parent.width : 280
        height: body.implicitHeight + head.height + 22
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 1, 1, 0.025); border.color: Theme.cardBorder
        Text {
            id: head
            x: 12; y: 10; text: parent.title
            color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        Column { id: body; x: 12; y: head.y + head.height + 8; width: parent.width - 24; spacing: 8 }
    }
    component Note: Text {
        width: parent ? parent.width : 260
        wrapMode: Text.Wrap
        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
    }
    component Channel: Item {
        property string name
        property color dot
        property int value
        width: parent ? parent.width : 260; height: 26
        Rectangle { y: 9; width: 8; height: 8; radius: 4; color: parent.dot }
        Text { x: 16; anchors.verticalCenter: parent.verticalCenter; text: parent.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
        Rectangle {
            x: 100; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 150; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle { width: parent.width * parent.parent.value / 100; height: parent.height; radius: 3; color: parent.parent.dot
                Behavior on width { NumberAnimation { duration: 90 } } }
        }
        Text {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: root.ctrl.hasScript ? parent.value : "–"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
    }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Text { anchors.centerIn: parent; text: root.collapsed ? "⟨" : "⟩"; color: Theme.textDim; font.pixelSize: 15 }
        MouseArea { id: tg; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.collapsed = !root.collapsed }
    }
    Text {
        visible: !root.collapsed
        x: 16; y: 14; text: qsTr("Inspector")
        color: Theme.textMuted; font.family: Theme.font; font.pixelSize: 11; font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
    }
    FlowTabs {
        id: tabs
        visible: !root.collapsed
        x: 10; y: 40
        model: [qsTr("Devices"), qsTr("Audio"), qsTr("Playback"), qsTr("View")]
        current: root.ctrl.uiValue("player.inspectorTab", 0)
        onPicked: (i) => { current = i; root.ctrl.setUiValue("player.inspectorTab", i) }
    }

    Flickable {
        visible: !root.collapsed
        anchors.fill: parent; anchors.margins: 12; anchors.topMargin: 82
        contentHeight: pages.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Item {
            id: pages
            width: parent.width
            height: Math.max(devices.height, audio.height, playback.height, view.height)

            // Devices: live channels. Output to a device comes with P2.
            Column {
                id: devices
                visible: tabs.current === 0
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Channels")
                    Channel { name: qsTr("Stroke"); dot: Theme.orange; value: root.ctrl.strokeNow }
                    Channel { name: qsTr("Vibration"); dot: Theme.laneVibration; value: root.ctrl.vibrationNow }
                    Channel { name: qsTr("Suction"); dot: Theme.laneSuction; value: root.ctrl.suctionNow }
                    Note { text: qsTr("Live values at the playhead. Sending them to a device (levels, effects, stroker range) comes with the devices phase (P2).") }
                }
            }
            // Audio: BPM. Vibration from the video's sound comes with P2.
            Column {
                id: audio
                visible: tabs.current === 1
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("BPM")
                    Row {
                        spacing: 8
                        FlowTextField {
                            width: 90; placeholder: root.ctrl.bpm > 0 ? String(root.ctrl.bpm) : qsTr("20–240")
                            onAccepted: root.ctrl.bpm = parseFloat(text) || 0
                        }
                        FlowButton { text: qsTr("BPM grid"); checked: root.ctrl.bpmGrid; onClicked: root.ctrl.bpmGrid = !root.ctrl.bpmGrid }
                    }
                    Note { text: qsTr("Beat lines on the curve (every 4th is a bar). Display only; the script is never changed. Detecting the BPM from the sound comes with the audio tempo port (P3).") }
                }
                Card {
                    title: qsTr("Audio Vibration")
                    Note { text: qsTr("Vibration from the video's sound plays on a device, so it comes with the devices phase (P2).") }
                }
            }
            // Playback: script tools.
            Column {
                id: playback
                visible: tabs.current === 2
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Script Tools")
                    Row {
                        spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Script offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        OffsetField { value: root.ctrl.offsetMs; onChanged: (v) => root.ctrl.offsetMs = v }
                    }
                    Row {
                        spacing: 8
                        FlowButton { text: qsTr("Reset"); enabled: root.ctrl.offsetMs !== 0; onClicked: root.ctrl.offsetMs = 0 }
                        FlowButton { text: qsTr("Bake offset"); enabled: false }
                    }
                    Note { text: qsTr("Positive: the script comes later. Saved per script. Baking it into the script's times (all channels, bookmarks and chapters in one safe step) comes with the Editor (P4).") }
                }
            }
            // View: what the timeline shows.
            Column {
                id: view
                visible: tabs.current === 3
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Timeline")
                    FlowButton {
                        text: qsTr("Audio level lane"); checked: root.ctrl.audioLane
                        onClicked: root.ctrl.audioLane = !root.ctrl.audioLane
                    }
                    Note { text: qsTr("Heatmap: click = seek, drag = mark. Wheel on the curve zooms.") }
                }
                Card {
                    title: qsTr("Curve editing")
                    Note { text: qsTr("Editing dots, range tools and Undo come with the Editor (P4).") }
                }
            }
        }
    }
}
