import QtQuick
import QtQuick.Dialogs
import QtMultimedia
import SamN

// Player mode: the video card with transport and heatmap, and the timeline
// card with the curve, the channel lanes and the audio level lane.
// All logic lives in PlayerController (C++); this file only shows it.
Item {
    id: page
    property alias controller: ctrl

    PlayerController { id: ctrl }

    MediaPlayer {
        id: media
        source: ctrl.videoUrl
        videoOutput: videoOut
        onDurationChanged: ctrl.setVideoDurationMs(duration)
        audioOutput: AudioOutput { id: audio; volume: volume.value }
        onPositionChanged: {
            ctrl.setVideoPositionMs(position)
            const jump = ctrl.loopJumpFor(position)
            if (jump >= 0) media.position = jump
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open video or script")
        nameFilters: [qsTr("Video or script (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.funscript *.samn)"), qsTr("All files (*)")]
        onAccepted: ctrl.openPath(selectedFile.toString())
    }
    FileDialog {
        id: scriptDialog
        title: qsTr("Choose a script for this video")
        nameFilters: [qsTr("Script (*.funscript *.samn)"), qsTr("All files (*)")]
        onAccepted: ctrl.openScript(selectedFile.toString(), ctrl.videoUrl.toString())
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Add a script folder")
        onAccepted: ctrl.addScriptFolder(selectedFolder.toString())
    }

    function openFile() { openDialog.open() }
    function togglePlay() { media.playbackState === MediaPlayer.PlayingState ? media.pause() : media.play() }
    function seekBy(ms) { media.position = Math.max(0, Math.min(media.duration, media.position + ms)) }

    // Header: what is open, the script variant, Open.
    Item {
        id: header
        width: parent.width; height: 40
        Text {
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            anchors.right: variantBox.visible ? variantBox.left : openBtn.left; anchors.rightMargin: 12
            text: ctrl.hasScript ? ctrl.scriptName + (ctrl.videoName !== "" ? "  ·  " + ctrl.videoName : "")
                                 : ctrl.videoName !== "" ? ctrl.videoName : qsTr("Open a video or a script")
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
            elide: Text.ElideMiddle
        }
        FlowCombo {
            id: variantBox
            anchors.right: openBtn.left; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            width: 240
            visible: ctrl.variants.length > 1
            model: ctrl.variants
            textRole: "label"
            currentIndex: ctrl.variantIndex
            onActivated: (i) => ctrl.chooseVariant(i)
        }
        FlowButton {
            id: openBtn
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            primary: true; text: qsTr("Open…")
            onClicked: page.openFile()
        }
    }

    // Errors stay visible until the next successful open.
    Rectangle {
        id: errorBar
        anchors.top: header.bottom; anchors.topMargin: ctrl.lastError !== "" ? 8 : 0
        width: parent.width; height: ctrl.lastError !== "" ? errText.implicitHeight + 16 : 0
        visible: height > 0; clip: true
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 0.35, 0.3, 0.12); border.color: Qt.rgba(1, 0.4, 0.35, 0.5)
        Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Text {
            id: errText
            anchors.verticalCenter: parent.verticalCenter; x: 14; width: parent.width - 28
            text: ctrl.lastError; wrapMode: Text.Wrap
            color: "#ffd0c8"; font.family: Theme.fontSoft; font.pixelSize: 13
        }
    }

    // Video card: stage + transport.
    GlassCard {
        id: videoCard
        anchors.top: errorBar.bottom; anchors.topMargin: 10
        width: parent.width
        height: Math.max(300, (parent.height - header.height) * 0.58)
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
            // Video first: a video without a script says so and offers the ways on.
            Rectangle {
                anchors.centerIn: parent
                visible: ctrl.noScriptForVideo
                width: Math.min(parent.width - 40, 460); height: noScript.implicitHeight + 40
                radius: Theme.radius
                color: Qt.rgba(4 / 255, 14 / 255, 28 / 255, 0.86); border.color: Theme.cardBorder
                Column {
                    id: noScript
                    anchors.centerIn: parent; width: parent.width - 40
                    spacing: 12
                    Image { source: "assets/empty-script.svg"; width: 64; height: 64; sourceSize: Qt.size(128, 128); anchors.horizontalCenter: parent.horizontalCenter }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No script found for this video"); color: Theme.text
                        font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                        text: ctrl.scriptFolders.length
                              ? qsTr("Searched beside the video and in %n script folder(s).", "", ctrl.scriptFolders.length)
                              : qsTr("Searched beside the video. Add a script folder to search there too.")
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter; spacing: 10
                        FlowButton { primary: true; text: qsTr("Choose script…"); onClicked: scriptDialog.open() }
                        FlowButton { text: qsTr("Add a script folder…"); onClicked: folderDialog.open() }
                    }
                    FlowButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Create script"); enabled: false
                    }
                    Text {
                        width: parent.width; horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Creating scripts comes with the Generator (P3).")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
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
            height: 132
            // Progress: blue → cyan → orange with a glowing knob.
            Item {
                id: progress
                x: 18; y: 12; width: parent.width - 36; height: 16
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
            // Heatmap: intensity over the whole script, under the progress bar.
            HeatmapItem {
                id: heat
                x: 18; y: 34; width: parent.width - 36; height: 10
                visible: !empty
                Component.onCompleted: ctrl.attachHeatmap(heat)
            }
            // Left: chapters and loop.
            Row {
                anchors.left: parent.left; anchors.leftMargin: 18
                y: 66; spacing: 8
                FlowCombo {
                    id: chapterBox
                    width: 190
                    visible: ctrl.chapters.length > 0
                    model: ctrl.chapters
                    textRole: "name"
                    displayText: qsTr("Chapters (%1)").arg(ctrl.chapters.length)
                    onActivated: (i) => media.position = Math.max(0, ctrl.chapterVideoMs(i))
                }
                FlowButton { text: qsTr("A"); onClicked: ctrl.setLoopStart(media.position); implicitWidth: 40 }
                FlowButton { text: qsTr("B"); onClicked: ctrl.setLoopEnd(media.position); implicitWidth: 40 }
                FlowButton {
                    text: qsTr("Loop off"); visible: ctrl.loopActive; checked: true
                    onClicked: ctrl.clearLoop()
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 54
                spacing: 18
                IconButton { glyph: "⟲"; tip: qsTr("Back 5 s"); onClicked: page.seekBy(-5000); anchors.verticalCenter: parent.verticalCenter }
                PlayButton {
                    playing: media.playbackState === MediaPlayer.PlayingState
                    onClicked: page.togglePlay()
                }
                IconButton { glyph: "⟳"; tip: qsTr("Forward 5 s"); onClicked: page.seekBy(5000); anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                anchors.right: parent.right; anchors.rightMargin: 18
                y: 74; spacing: 10
                Text { text: "🔊"; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                FlowSlider { id: volume; from: 0; to: 1; value: 0.8; width: 130; onMoved: (v) => value = v }
            }
        }
    }

    // Timeline card: the curve, the channel lanes and the audio level.
    GlassCard {
        id: timeline
        anchors.top: videoCard.bottom; anchors.topMargin: 12
        anchors.bottom: parent.bottom
        width: parent.width
        Row {
            x: 16; y: 10; spacing: 10
            Rectangle {
                width: 86; height: 28; radius: 14; color: "transparent"; border.color: Theme.orange
                Text { anchors.centerIn: parent; text: qsTr("Stroke"); color: "white"; font.family: Theme.fontSoft; font.pixelSize: 13 }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ctrl.hasScript ? qsTr("%1 points").arg(ctrl.pointCount) : ""
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
            }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 16; y: 8; spacing: 8
            Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            OffsetField { value: ctrl.offsetMs; onChanged: (v) => ctrl.offsetMs = v }
        }
        // Zoom (0.7): wheel on the curve, buttons around the playhead.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter; y: 8; spacing: 6
            visible: ctrl.hasScript
            IconButton { width: 32; height: 32; glyph: "−"; tip: qsTr("Zoom out"); onClicked: ctrl.zoomBy(1 / 1.6) }
            Text {
                width: 110; anchors.verticalCenter: parent.verticalCenter; horizontalAlignment: Text.AlignHCenter
                text: ctrl.zoomLabel; color: ctrl.zoomed ? Theme.cyan : Theme.textDim
                font.family: Theme.fontSoft; font.pixelSize: 12
            }
            IconButton { width: 32; height: 32; glyph: "+"; tip: qsTr("Zoom in"); onClicked: ctrl.zoomBy(1.6) }
            FlowButton { text: qsTr("Full"); visible: ctrl.zoomed; implicitHeight: 32; onClicked: ctrl.resetZoom() }
        }
        Column {
            id: lanes
            x: 16; y: 46
            width: parent.width - 32; height: parent.height - 58
            readonly property int laneH: 34
            readonly property int extraLanes: (ctrl.hasScript ? 2 : 0) + (audioLane.empty ? 0 : 1)
            CurveItem {
                id: curve
                width: parent.width
                height: Math.max(60, parent.height - lanes.extraLanes * lanes.laneH)
                lane: "stroke"; lineColor: Theme.orange; showGrid: true
                viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                Component.onCompleted: ctrl.attachCurve(curve)
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / curve.width)
                }
                Text {
                    anchors.centerIn: parent
                    visible: curve.empty
                    text: qsTr("No script loaded"); color: Theme.textMuted; font.family: Theme.fontSoft
                }
            }
            CurveItem {
                id: vibLane
                width: parent.width; height: lanes.laneH
                visible: ctrl.hasScript
                lane: "vibration"; lineColor: Theme.laneVibration
                label: ctrl.vibrationFromScript ? qsTr("VIBRATION") : qsTr("VIBRATION · device preview")
                lineWidth: 1.15; glow: 4; fill: 0.2
                viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                Component.onCompleted: ctrl.attachCurve(vibLane)
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
            }
            CurveItem {
                id: sucLane
                width: parent.width; height: lanes.laneH
                visible: ctrl.hasScript
                lane: "suction"; lineColor: Theme.laneSuction
                label: ctrl.suctionFromScript ? qsTr("SUCTION") : qsTr("SUCTION · device preview")
                lineWidth: 1.15; glow: 4; fill: 0.2
                viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                Component.onCompleted: ctrl.attachCurve(sucLane)
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
            }
            AudioLevelItem {
                id: audioLane
                width: parent.width; height: lanes.laneH
                visible: !empty
                source: ctrl.videoUrl
                viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                offsetMs: ctrl.offsetMs
                playheadMs: ctrl.playheadMs
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
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
    Shortcut { sequence: "A"; onActivated: ctrl.setLoopStart(media.position) }
    Shortcut { sequence: "B"; onActivated: ctrl.setLoopEnd(media.position) }
    Shortcut { sequence: StandardKey.Open; onActivated: page.openFile() }
}
