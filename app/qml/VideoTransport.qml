import QtQuick
import SamN

// The transport under a video, shared by the Player and the Generator (D-154):
// the progress bar (blue → cyan → orange, glowing knob), a line of info, the
// page's own controls on the left, Previous · −10 s · Play · +10 s · Stop
// (· Next) in the middle, mute and volume on the right. The page owns the
// player; this only shows it and reports what the user wants.
Item {
    id: root
    property real position: 0
    property real duration: 0
    property bool playing: false
    property bool muted: false
    property alias volume: volumeSlider.value
    // Under the progress bar (the Player's live channel values).
    property string info: ""
    property bool canPrevious: true
    property bool showNext: false
    // The page's own controls, left of the buttons.
    property alias leftItems: leftControls.data
    // A lower bar for the Generator, where the curves need the room.
    property bool compact: false

    signal seekRequested(real ms)
    signal seekByRequested(real ms)
    signal togglePlayRequested()
    signal stopRequested()
    signal previousRequested()
    signal nextRequested()
    signal muteToggled()
    signal volumeMoved(real value)

    height: compact ? 84 : 132

    Item {
        id: progress
        objectName: "transportProgress"
        x: 18; y: root.compact ? 8 : 12; width: parent.width - 36; height: 16
        property real frac: root.duration > 0 ? Math.min(1, root.position / root.duration) : 0
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.10) }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * progress.frac; height: 6; radius: 3
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.blue }
                GradientStop { position: 0.55; color: Theme.cyan }
                GradientStop { position: 1; color: Theme.orange }
            }
        }
        Rectangle {
            x: parent.width * progress.frac - 8; anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16; radius: 8
            color: "#dff8ff"; border.color: Theme.blue; border.width: 3
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            function seek(mx) { if (root.duration > 0) root.seekRequested(Math.max(0, Math.min(1, mx / width)) * root.duration) }
            onPressed: (m) => seek(m.x)
            onPositionChanged: (m) => { if (pressed) seek(m.x) }
        }
    }
    Text {
        textFormat: Text.PlainText
        x: 18; y: 34
        visible: root.info !== "" && !root.compact
        text: root.info
        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
    }
    Row {
        id: leftControls
        anchors.left: parent.left; anchors.leftMargin: 18
        y: root.compact ? 40 : 66; spacing: 8
    }
    // Centred in the gap between the side controls, so nothing overlaps on a
    // narrow card.
    Row {
        id: transportRow
        readonly property real gapLeft: leftControls.x + leftControls.width
        x: gapLeft + Math.max(0, (rightControls.x - gapLeft - width) / 2)
        y: root.compact ? 28 : 54
        spacing: 12
        IconButton {
            objectName: "previousButton"
            width: 36; height: 36
            icon: "skipBack"; tip: qsTr("Previous item, or back to the start")
            enabled: root.canPrevious
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.previousRequested()
        }
        IconButton { objectName: "back10Button"; icon: "back10"; tip: qsTr("Back 10 s"); onClicked: root.seekByRequested(-10000); anchors.verticalCenter: parent.verticalCenter }
        PlayButton {
            objectName: "transportPlay"
            width: root.compact ? 50 : 72; height: width
            playing: root.playing
            onClicked: root.togglePlayRequested()
        }
        IconButton { objectName: "forward10Button"; icon: "forward10"; tip: qsTr("Forward 10 s"); onClicked: root.seekByRequested(10000); anchors.verticalCenter: parent.verticalCenter }
        // Stop: the device stops and the playhead goes back to 0.
        IconButton {
            objectName: "stopButton"
            width: 36; height: 36
            icon: "stop"; tip: qsTr("Stop")
            enabled: root.playing || root.position > 0
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.stopRequested()
        }
        IconButton {
            width: 36; height: 36
            icon: "skipForward"; tip: qsTr("Next item in the list")
            visible: root.showNext
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.nextRequested()
        }
    }
    Row {
        id: rightControls
        anchors.right: parent.right; anchors.rightMargin: 18
        y: root.compact ? 44 : 74; spacing: 10
        IconButton {
            width: 36; height: 36; icon: root.muted ? "mute" : "volume"; tip: qsTr("Mute")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.muteToggled()
        }
        FlowSlider {
            id: volumeSlider; objectName: "volume"
            from: 0; to: 1; width: 110; anchors.verticalCenter: parent.verticalCenter
            onMoved: (v) => root.volumeMoved(v)
        }
    }
}
