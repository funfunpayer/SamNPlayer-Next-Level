import QtQuick
import QtQuick.Dialogs
import SamN

// The Inspector of the Player (0.7 v1): tabs Devices · Audio · Playback · View.
GlassCard {
    id: root
    required property PlayerController ctrl
    // The one device connection of the app (Main.qml).
    required property DeviceController device
    property bool collapsed: false
    property alias tab: tabs.current
    signal seekRequested(real videoMs)
    signal addToListRequested()
    // The video position (for Save project) and the project picker (the page owns the dialog).
    property real videoPositionMs: 0
    signal projectLoadRequested()
    // Edit the open script in the Generator's Editor (the scenes when true).
    signal editRequested(bool scenes)
    property string projectStatus: ""
    width: collapsed ? 44 : 320
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    function laneTitle(lane) {
        return ({
            stroke: qsTr("Stroke"), vibration: qsTr("Vibration"), suction: qsTr("Suction"),
            surge: qsTr("Surge"), sway: qsTr("Sway"), twist: qsTr("Twist"),
            roll: qsTr("Roll"), pitch: qsTr("Pitch")
        })[lane] || lane
    }
    function axisTitle(axis) { return laneTitle(axis) }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Icon { anchors.centerIn: parent; name: root.collapsed ? "chevronLeft" : "chevronRight"; width: 16; height: 16; color: Theme.textDim }
        MouseArea {
            id: tg
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.collapsed = !root.collapsed
            HoverHandler { id: collapseHover }
            ToolTipText {
                shown: collapseHover.hovered
                text: root.collapsed
                      ? qsTr("Show Inspector — or press T for theatre (panels away)")
                      : qsTr("Hide Inspector — bigger video (T = theatre, panels away)")
            }
        }
    }
    Text {
        textFormat: Text.PlainText
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

            InspectorDevices {
                id: devices
                visible: tabs.current === 0
                width: parent.width
                ctrl: root.ctrl
                device: root.device
                rootPanel: root
            }
            InspectorAudio {
                id: audio
                visible: tabs.current === 1
                width: parent.width
                ctrl: root.ctrl
                device: root.device
                rootPanel: root
            }
            InspectorPlayback {
                id: playback
                visible: tabs.current === 2
                width: parent.width
                ctrl: root.ctrl
                device: root.device
                rootPanel: root
            }
            InspectorView {
                id: view
                visible: tabs.current === 3
                width: parent.width
                ctrl: root.ctrl
                device: root.device
                rootPanel: root
            }
        }
    }
}
