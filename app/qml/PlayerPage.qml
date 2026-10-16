import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtMultimedia
import SamN

// Player mode: video card with transport, timeline card with the curve.
// All logic lives in PlayerController (C++); this file only shows it.
Item {
    id: page
    property alias controller: ctrl

    PlayerController { id: ctrl }

    MediaPlayer {
        id: media
        source: ctrl.videoUrl
        videoOutput: videoOut
        audioOutput: AudioOutput { id: audio; volume: volumeSlider.value }
        onPositionChanged: ctrl.setVideoPositionMs(position)
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open video or script")
        nameFilters: [qsTr("Video or script (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.funscript)"), qsTr("All files (*)")]
        onAccepted: ctrl.openPath(selectedFile.toString())
    }

    function openFile() { openDialog.open() }
    function togglePlay() { media.playbackState === MediaPlayer.PlayingState ? media.pause() : media.play() }
    function seekBy(ms) { media.position = Math.max(0, media.position + ms) }

    // Header: script path and Open.
    Row {
        id: header
        x: 0; y: 0; height: 44
        spacing: 12
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: ctrl.hasScript ? ctrl.scriptPath : qsTr("Open a video or a funscript")
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
            elide: Text.ElideMiddle; width: page.width - openBtn.width - 24
        }
    }
    Rectangle {
        id: openBtn
        anchors.right: parent.right; y: 0
        width: 120; height: 40; radius: 12
        color: openArea.containsMouse ? "#25d6ff" : Theme.cyan
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Text { anchors.centerIn: parent; text: qsTr("Open…"); color: "#02101f"; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold }
        MouseArea { id: openArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: page.openFile() }
    }

    // Video card: stage + transport.
    GlassCard {
        id: videoCard
        anchors.top: header.bottom; anchors.topMargin: 10
        width: parent.width
        height: Math.max(280, (parent.height - header.height) * 0.6)
        Rectangle {
            id: stage
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 1
            height: parent.height - transport.height
            radius: Theme.radius
            color: "#02070f"
            clip: true
            VideoOutput {
                id: videoOut
                anchors.fill: parent
                visible: ctrl.videoName !== ""
            }
            Column {
                anchors.centerIn: parent
                visible: ctrl.videoName === ""
                spacing: 10
                Image { source: "assets/empty-video.svg"; width: 120; height: 90; sourceSize: Qt.size(240, 180); anchors.horizontalCenter: parent.horizontalCenter; opacity: 0.8 }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: ctrl.hasScript ? qsTr("No video beside this script") : qsTr("Drop a video or script here, or press Open…")
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
                }
            }
            Text {
                anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 14
                text: ctrl.formatTime(media.position) + " / " + ctrl.formatTime(media.duration)
                color: "white"; font.family: Theme.fontSoft; font.pixelSize: 13
                visible: ctrl.videoName !== ""
            }
        }
        Item {
            id: transport
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            height: 128
            // Progress: cyan → orange with a glowing knob.
            Item {
                id: progress
                x: 18; y: 14; width: parent.width - 36; height: 16
                property real frac: media.duration > 0 ? media.position / media.duration : 0
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
                    function seek(mx) { if (media.duration > 0) media.position = Math.max(0, Math.min(1, mx / width)) * media.duration }
                    onPressed: (m) => seek(m.x)
                    onPositionChanged: (m) => { if (pressed) seek(m.x) }
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 40
                spacing: 18
                RoundButton { text: "⟲"; onClicked: page.seekBy(-5000); anchors.verticalCenter: parent.verticalCenter }
                PlayButton {
                    playing: media.playbackState === MediaPlayer.PlayingState
                    onClicked: page.togglePlay()
                }
                RoundButton { text: "⟳"; onClicked: page.seekBy(5000); anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 18
                y: 64; spacing: 8
                Text { text: "🔊"; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Slider { id: volumeSlider; from: 0; to: 1; value: 0.8; width: 120 }
            }
        }
    }

    // Timeline card: curve with playhead, offset.
    GlassCard {
        anchors.top: videoCard.bottom; anchors.topMargin: 12
        anchors.bottom: parent.bottom
        width: parent.width
        Row {
            x: 16; y: 12; spacing: 10
            Rectangle {
                width: 86; height: 28; radius: 14; color: "transparent"; border.color: Theme.orange
                Text { anchors.centerIn: parent; text: qsTr("Stroke"); color: "white"; font.family: Theme.fontSoft; font.pixelSize: 13 }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; text: ctrl.hasScript ? qsTr("%1 points").arg(ctrl.pointCount) : ""; color: Theme.textDim; font.pixelSize: 12 }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 16; y: 8; spacing: 8
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Offset"); color: Theme.textDim; font.pixelSize: 13 }
            SpinBox {
                id: offsetBox
                from: -10000; to: 10000; stepSize: 50; editable: true
                value: ctrl.offsetMs
                onValueModified: ctrl.offsetMs = value
                textFromValue: (v) => v + " ms"
                valueFromText: (t) => parseInt(t)
            }
        }
        CurveItem {
            id: curve
            x: 16; y: 48
            width: parent.width - 32; height: parent.height - 64
            lineColor: Theme.orange
            Component.onCompleted: ctrl.attachCurve(curve)
            Text {
                anchors.centerIn: parent
                visible: curve.empty
                text: qsTr("No script loaded"); color: Theme.textMuted; font.family: Theme.fontSoft
            }
        }
    }

    // Drop a video or a script anywhere on the page.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => { if (drop.hasUrls) ctrl.openPath(drop.urls[0].toString()) }
    }

    Shortcut { sequence: "Space"; onActivated: page.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: page.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: page.seekBy(5000) }
    Shortcut { sequence: "Shift+Left"; onActivated: page.seekBy(-1000) }
    Shortcut { sequence: "Shift+Right"; onActivated: page.seekBy(1000) }
    Shortcut { sequence: "+"; onActivated: ctrl.offsetMs = ctrl.offsetMs + 50 }
    Shortcut { sequence: "-"; onActivated: ctrl.offsetMs = ctrl.offsetMs - 50 }
    Shortcut { sequence: StandardKey.Open; onActivated: page.openFile() }
}
