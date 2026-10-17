import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtMultimedia
import SamN

// Player mode: the video card with transport and heatmap, and the timeline
// card with the curve, the channel lanes and the audio level lane.
// All logic lives in PlayerController (C++); this file only shows it.
Item {
    id: page
    property alias controller: ctrl

    property real speed: 1.0
    // Nothing open yet: the Flow stage welcomes (0.7 "Ready when you are").
    readonly property bool empty: !ctrl.hasScript && ctrl.videoName === ""
    // Video fullscreen (0.7: button, double-click, Esc leaves).
    property bool fullStage: false
    onFullStageChanged: {
        const w = page.Window.window
        if (w) w.visibility = fullStage ? Window.FullScreen : Window.Windowed
    }
    property bool muted: false
    // The mode deck uses its number keys while it is open.
    property bool deckOpen: false

    // The device connection of the app (Main.qml); the Player drives it.
    required property DeviceController device

    PlayerController {
        id: ctrl
        device: page.device
        onSleepReached: media.pause()
        // The Emergency stop pauses the video too (the device is already stopped).
        onEmergencyStopped: media.pause()
    }
    Binding { target: ctrl.output; property: "speed"; value: page.speed }

    MediaPlayer {
        id: media
        objectName: "media"
        source: ctrl.videoUrl
        videoOutput: videoOut
        onDurationChanged: ctrl.setVideoDurationMs(media.duration)
        audioOutput: AudioOutput { id: audio; volume: volume.value; muted: page.muted }
        playbackRate: page.speed
        onPlaybackStateChanged: ctrl.setPlaying(playbackState === MediaPlayer.PlayingState)
        // Play list: at the end of the video, Auto-advance opens the next
        // script and it starts once its video is loaded (0.7).
        property bool playWhenLoaded: false
        // A seek before the video is loaded (a project's saved position)
        // waits here; clamped to a duration of 0 it would land at 0.
        property real pendingSeekMs: -1
        onSourceChanged: pendingSeekMs = -1
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && pendingSeekMs >= 0) {
                const ms = pendingSeekMs
                pendingSeekMs = -1
                page.seekTo(ms)
            }
            if (mediaStatus === MediaPlayer.EndOfMedia && ctrl.playlistFinished()) playWhenLoaded = ctrl.videoName !== ""
            else if (mediaStatus === MediaPlayer.LoadedMedia && playWhenLoaded) { playWhenLoaded = false; media.play() }
        }
        onPositionChanged: {
            ctrl.setVideoPositionMs(media.position)
            const jump = ctrl.loopJumpFor(media.position)
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
    FileDialog {
        id: videoDialog
        title: qsTr("Select a video file")
        nameFilters: [qsTr("Video (*.mp4 *.mkv *.webm *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: ctrl.attachVideo(selectedFile.toString())
    }
    FileDialog {
        id: snapshotDialog
        title: qsTr("Save the frame as PNG")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG image (*.png)")]
        onAccepted: ctrl.saveSnapshot(videoOut.videoSink, selectedFile.toString())
    }
    FileDialog {
        id: addListDialog
        title: qsTr("Add a script to the list")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Script (*.funscript *.samn)"), qsTr("All files (*)")]
        onAccepted: { for (const f of selectedFiles) ctrl.addToPlaylist(f.toString()) }
    }
    FileDialog {
        id: projectDialog
        title: qsTr("Choose project (.snp.json)")
        nameFilters: [qsTr("Emotion project (*.snp.json)"), qsTr("JSON (*.json)")]
        onAccepted: {
            const path = selectedFile.toString()
            const seek = ctrl.loadProject(path)
            inspector.projectStatus = seek >= 0 ? qsTr("Project: %1").arg(ctrl.localPath(path)) : ctrl.lastError
            if (seek > 0) page.seekTo(seek)
        }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Add a script folder")
        onAccepted: ctrl.addScriptFolder(selectedFolder.toString())
    }

    function openFile() { openDialog.open() }
    function togglePlay() { media.playbackState === MediaPlayer.PlayingState ? media.pause() : media.play() }
    function seekBy(ms) { media.position = Math.max(0, Math.min(media.duration, media.position + ms)) }
    function seekTo(ms) {
        if (ms < 0) return
        if (media.duration <= 0 || media.mediaStatus === MediaPlayer.LoadingMedia) media.pendingSeekMs = ms
        else media.position = Math.max(0, Math.min(media.duration, ms))
    }
    function showTip(item, x, y, text) {
        if (text === "") { chartTip.visible = false; return }
        const p = item.mapToItem(page, x, y)
        tipText.text = text
        chartTip.x = Math.min(p.x + 12, page.width - chartTip.width - 4)
        chartTip.y = p.y + 12 + chartTip.height > page.height ? p.y - chartTip.height - 12 : p.y + 12
        chartTip.visible = true
    }

    // Library (left) · player (centre) · Inspector (right), as 0.7 v1.
    LibraryController { id: library }
    Connections {
        target: ctrl
        function onScriptOpened(path) { library.noteScriptOpened(path) }
        function onVideoLinked(path) { library.noteVideoOpened(path) }
        function onScriptEdited(path) { library.noteScriptEdited(path) }
    }
    LibraryPanel {
        id: libraryPanel
        visible: !page.fullStage
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        library: library
        currentPath: ctrl.scriptPath
        collapsed: ctrl.uiValue("player.libraryCollapsed", false)
        onCollapsedChanged: ctrl.setUiValue("player.libraryCollapsed", collapsed)
        onOpenFile: page.openFile()
        onOpenPath: (path) => ctrl.openPath(path)
        onShowAudioAnalysis: { bottomTabs.current = 4; ctrl.setUiValue("player.bottomTab", 4) }
        onShowBpm: { inspector.tab = 1; ctrl.setUiValue("player.inspectorTab", 1) }
    }
    InspectorPanel {
        id: inspector
        visible: !page.fullStage
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        ctrl: ctrl
        device: page.device
        collapsed: ctrl.uiValue("player.inspectorCollapsed", false)
        onCollapsedChanged: ctrl.setUiValue("player.inspectorCollapsed", collapsed)
        onSeekRequested: (ms) => page.seekTo(ms)
        onAddToListRequested: addListDialog.open()
        videoPositionMs: media.position
        onProjectLoadRequested: projectDialog.open()
    }
    Item {
        id: center
        anchors.left: page.fullStage ? parent.left : libraryPanel.right; anchors.leftMargin: page.fullStage ? 0 : 14
        anchors.right: page.fullStage ? parent.right : inspector.left; anchors.rightMargin: page.fullStage ? 0 : 14
        anchors.top: parent.top; anchors.bottom: parent.bottom

        // Header: what is open, the script variant, Open.
        Item {
            id: header
            visible: !page.fullStage
            width: parent.width; height: 40
            Text {
                textFormat: Text.PlainText
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                anchors.right: variantBox.visible ? variantBox.left : videoBar.visible ? videoBar.left : openBtn.left
                anchors.rightMargin: 12
                text: ctrl.hasScript ? ctrl.scriptName + (ctrl.videoName !== "" ? "  ·  " + ctrl.videoName : "")
                                     : ctrl.videoName !== "" ? ctrl.videoName : qsTr("Open a video or a script")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
                elide: Text.ElideMiddle
            }
            FlowCombo {
                id: variantBox
                anchors.right: videoBar.visible ? videoBar.left : openBtn.left; anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 240
                visible: ctrl.variants.length > 1
                model: ctrl.variants
                textRole: "label"
                currentIndex: ctrl.variantIndex
                onActivated: (i) => ctrl.chooseVariant(i)
            }
            // Video bar (0.7 v1): link a video, bookmark, snapshot.
            Row {
                id: videoBar
                anchors.right: fsBtn.visible ? fsBtn.left : openBtn.left; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                visible: ctrl.hasScript
                FlowButton {
                    icon: "video"
                    text: ctrl.videoName !== "" ? ctrl.videoName : qsTr("Select a video file…")
                    onClicked: videoDialog.open()
                }
                IconButton {
                    width: 36; height: 36; icon: "bookmark"; tip: qsTr("Add a bookmark at the current time")
                    onClicked: ctrl.addBookmark("", media.position)
                }
                IconButton {
                    width: 36; height: 36; icon: "snapshot"; tip: qsTr("Save the current video frame as PNG")
                    visible: ctrl.videoName !== ""
                    onClicked: {
                        snapshotDialog.currentFile = ctrl.snapshotFolder() + "/" + ctrl.snapshotName(media.position)
                        snapshotDialog.open()
                    }
                }
            }
            IconButton {
                id: fsBtn
                anchors.right: openBtn.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                width: 36; height: 36; icon: page.fullStage ? "minimize" : "fullscreen"
                tip: qsTr("Fullscreen (double-click the video)")
                visible: ctrl.videoName !== ""
                onClicked: page.fullStage = !page.fullStage
            }
            FlowButton {
                id: openBtn
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                primary: !page.empty; icon: "folder"; text: qsTr("Open…")
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
                textFormat: Text.PlainText
                id: errText
                anchors.verticalCenter: parent.verticalCenter; x: 14; width: parent.width - 28
                text: ctrl.lastError; wrapMode: Text.Wrap
                color: "#ffd0c8"; font.family: Theme.fontSoft; font.pixelSize: 13
            }
        }

        // Empty state (Owner 15 Oct, 0.8 #550): one real round Play button,
        // "Open…" under it, centred. No wordmark or stage art.
        Column {
            id: emptyState
            visible: page.empty
            anchors.centerIn: parent
            spacing: 18
            PlayButton {
                objectName: "emptyPlay"
                anchors.horizontalCenter: parent.horizontalCenter
                width: 168; height: 168
                onClicked: page.openFile()
            }
            Text {
                textFormat: Text.PlainText
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Open…"); color: emptyOpen.containsMouse ? "white" : Theme.text
                font.family: Theme.font; font.pixelSize: 20; font.weight: Font.DemiBold
                MouseArea {
                    id: emptyOpen
                    anchors.fill: parent; anchors.margins: -8
                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: page.openFile()
                }
            }
        }
        // Video card: stage + transport.
        GlassCard {
            id: videoCard
            visible: !page.empty
            anchors.top: page.fullStage ? parent.top : errorBar.bottom; anchors.topMargin: page.fullStage ? 0 : 10
            width: parent.width
            height: page.fullStage ? parent.height : Math.max(300, (parent.height - header.height) * 0.58)
            Rectangle {
                id: stage
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 1
                height: parent.height - transport.height
                radius: Theme.radius
                color: "#02070f"
                clip: true
                // Silk drifts behind the video (0.8 #550); the video covers
                // it where it plays, the bands show in the letterbox and
                // when there is no video.
                SilkLayer { running: stage.visible && Qt.application.state === Qt.ApplicationActive }
                VideoOutput {
                    id: videoOut
                    anchors.fill: parent
                    visible: ctrl.videoName !== ""
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: ctrl.videoName !== ""
                    onDoubleClicked: page.fullStage = !page.fullStage
                }
                PositionGauge {
                    anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                    visible: ctrl.hasScript && ctrl.posOverlay
                    value: ctrl.playheadMs >= 0 ? ctrl.strokeNow : -1
                }
                Column {
                    anchors.centerIn: parent
                    visible: ctrl.videoName === ""
                    spacing: 10
                    Image { source: "assets/empty-video.svg"; width: 120; height: 90; sourceSize: Qt.size(240, 180); anchors.horizontalCenter: parent.horizontalCenter; opacity: 0.8 }
                    Text {
                        textFormat: Text.PlainText
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
                            textFormat: Text.PlainText
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: qsTr("No script found for this video"); color: Theme.text
                            font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold
                        }
                        Text {
                            textFormat: Text.PlainText
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
                            textFormat: Text.PlainText
                            width: parent.width; horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Creating scripts comes with the Generator (P3).")
                            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
                Text {
                    textFormat: Text.PlainText
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
                // Live channel values at the playhead (0.7 stat row).
                Text {
                    textFormat: Text.PlainText
                    x: 18; y: 34
                    visible: ctrl.hasScript
                    text: qsTr("Vibration %1   ·   Suction %2").arg(ctrl.vibrationNow).arg(ctrl.suctionNow)
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                // Left (0.7 transport): sleep timer, loop selection, speed.
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 18
                    y: 66; spacing: 8
                    FlowCombo {
                        width: 108
                        model: [qsTr("Sleep off"), "15 min", "30 min", "45 min", "60 min", "90 min"]
                        readonly property var minutes: [0, 15, 30, 45, 60, 90]
                        currentIndex: Math.max(0, minutes.indexOf(ctrl.sleepMinutes))
                        onActivated: (i) => ctrl.sleepMinutes = minutes[i]
                    }
                    FlowButton {
                        text: qsTr("Loop"); checked: ctrl.loopEnabled
                        onClicked: ctrl.loopEnabled = !ctrl.loopEnabled
                    }
                    FlowCombo {
                        width: 78
                        model: ["0.5×", "0.75×", "1.0×", "1.25×", "1.5×", "2.0×"]
                        readonly property var rates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                        currentIndex: rates.indexOf(page.speed)
                        onActivated: (i) => page.speed = rates[i]
                    }
                }
                Row {
                    id: transportRow
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 54
                    spacing: 18
                    IconButton { icon: "back10"; tip: qsTr("Back 10 s"); onClicked: page.seekBy(-10000); anchors.verticalCenter: parent.verticalCenter }
                    PlayButton {
                        playing: media.playbackState === MediaPlayer.PlayingState
                        onClicked: page.togglePlay()
                    }
                    IconButton { icon: "forward10"; tip: qsTr("Forward 10 s"); onClicked: page.seekBy(10000); anchors.verticalCenter: parent.verticalCenter }
                }
                IconButton {
                    icon: "skipForward"; tip: qsTr("Next item in the list")
                    // Beside the centred row, so Play stays in the middle.
                    visible: ctrl.playlistHasNext
                    anchors.left: transportRow.right; anchors.leftMargin: 18
                    anchors.verticalCenter: transportRow.verticalCenter
                    onClicked: {
                        const wasPlaying = media.playbackState === MediaPlayer.PlayingState
                        media.stop()
                        if (ctrl.playlistNext()) media.playWhenLoaded = wasPlaying && ctrl.videoName !== ""
                    }
                }
                Row {
                    anchors.right: parent.right; anchors.rightMargin: 18
                    y: 74; spacing: 10
                    IconButton {
                        width: 36; height: 36; icon: page.muted ? "mute" : "volume"; tip: qsTr("Mute")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: page.muted = !page.muted
                    }
                    FlowSlider { id: volume; from: 0; to: 1; value: 0.8; width: 110; anchors.verticalCenter: parent.verticalCenter; onMoved: (v) => value = v }
                }
            }
        }

        // Timeline card: the curve, the channel lanes and the audio level.
        GlassCard {
            id: timeline
            visible: !page.empty && !page.fullStage
            anchors.top: videoCard.bottom; anchors.topMargin: 12
            anchors.bottom: parent.bottom
            width: parent.width
            // Bottom tabs (0.7 v1): Timeline · Chapters · Bookmarks · Scene Map · Audio Analysis.
            FlowTabs {
                id: bottomTabs
                x: 16; y: 9
                model: [qsTr("Timeline"), qsTr("Chapters"), qsTr("Bookmarks"), qsTr("Scene Map"), qsTr("Audio Analysis")]
                current: ctrl.uiValue("player.bottomTab", 0)
                onPicked: (i) => { current = i; ctrl.setUiValue("player.bottomTab", i) }
            }
            Row {
                id: offsetRow
                anchors.right: parent.right; anchors.rightMargin: 16; y: 8; spacing: 8
                Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                OffsetField { value: ctrl.offsetMs; onChanged: (v) => ctrl.offsetMs = v }
            }
            // Lane names left of the lanes (0.7 v1), one per visible row.
            Column {
                id: laneNames
                visible: lanes.visible
                x: 16; y: 46; width: 108
                Item { width: 1; height: ruler.height; visible: ruler.visible }
                LaneName { name: qsTr("Stroke"); dot: Theme.orange; height: curve.height; visible: ctrl.showStrokeLane }
                LaneName { name: qsTr("Vibration"); dot: Theme.laneVibration; height: vibLane.height; visible: ctrl.showVibrationLane }
                LaneName { name: qsTr("Suction"); dot: Theme.laneSuction; height: sucLane.height; visible: ctrl.showSuctionLane }
                LaneName { name: qsTr("Audio Level"); dot: "#93aec4"; height: audioLane.height; visible: audioLane.visible }
                LaneName { name: qsTr("Chapters"); dot: "#2f80ed"; height: chapterStrip.height; visible: chapterStrip.visible }
                Item { width: 1; height: 4; visible: heatBar.visible }
                LaneName { name: qsTr("Heatmap"); dot: "#e8603c"; height: heatBar.height; visible: heatBar.visible }
            }
            Column {
                id: lanes
                visible: bottomTabs.current === 0
                x: 16 + 112; y: 46
                width: parent.width - 32 - 112; height: parent.height - 58
                // D-132: lanes the device cannot use are hidden; without the stroke
                // lane the device's lanes share its height.
                readonly property int audioH: 34
                readonly property int laneH: curve.visible ? 34
                    : Math.max(34, Math.min(140, (height - fixedH - (audioLane.visible ? audioH : 0)) / Math.max(1, (vibLane.visible ? 1 : 0) + (sucLane.visible ? 1 : 0))))
                readonly property int extraLanes: (vibLane.visible ? 1 : 0) + (sucLane.visible ? 1 : 0)
                readonly property int fixedH: ruler.height + (heatBar.visible ? heatBar.height + 4 : 0) + markRow.height
                                              + (chapterStrip.visible ? chapterStrip.height : 0)
                RulerItem {
                    id: ruler
                    width: parent.width; height: 22
                    visible: ctrl.hasScript
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    playheadMs: ctrl.playheadMs
                    textColor: Theme.textDim; markColor: Theme.laneVibration
                }
                CurveItem {
                    id: curve
                    width: parent.width
                    visible: ctrl.showStrokeLane || !ctrl.hasScript
                    height: Math.max(60, parent.height - lanes.extraLanes * lanes.laneH - (audioLane.visible ? lanes.audioH : 0) - lanes.fixedH)
                    lane: "stroke"; lineColor: Theme.orange; showGrid: true
                    markerStartMs: ctrl.markerStartMs; markerEndMs: ctrl.markerEndMs
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    Component.onCompleted: ctrl.attachCurve(curve)
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / curve.width)
                    }
                    // Hover shows time and position; a click seeks (0.7).
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        onPositionChanged: (m) => page.showTip(curve, m.x, m.y, curve.hoverText(m.x))
                        onExited: chartTip.visible = false
                        onClicked: (m) => page.seekTo(Math.round(curve.msAtX(m.x)) + ctrl.offsetMs)
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        visible: curve.empty
                        text: qsTr("No script loaded"); color: Theme.textMuted; font.family: Theme.fontSoft
                    }
                }
                CurveItem {
                    id: vibLane
                    width: parent.width; height: lanes.laneH
                    visible: ctrl.showVibrationLane  // D-132: the script's channels, or the device's
                    lane: "vibration"; lineColor: Theme.laneVibration
                    lineWidth: 1.15; glow: 4; fill: 0.2
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    Component.onCompleted: ctrl.attachCurve(vibLane)
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / parent.width)
                    }
                    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                }
                CurveItem {
                    id: sucLane
                    width: parent.width; height: lanes.laneH
                    visible: ctrl.showSuctionLane
                    lane: "suction"; lineColor: Theme.laneSuction
                    lineWidth: 1.15; glow: 4; fill: 0.2
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    Component.onCompleted: ctrl.attachCurve(sucLane)
                    WheelHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / parent.width)
                    }
                    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                }
                AudioLevelItem {
                    id: audioLane
                    width: parent.width; height: lanes.audioH
                    visible: !empty && ctrl.audioLane
                    source: ctrl.videoUrl
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    offsetMs: ctrl.offsetMs
                    playheadMs: ctrl.playheadMs
                    label: ""
                    Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
                }
                // Chapter bands (0.7 pb-chapter-bands): one coloured band per
                // chapter on the curve's time scale; click = seek to its start.
                Item {
                    id: chapterStrip
                    width: parent.width; height: 30
                    visible: ctrl.hasScript
                    clip: true
                    readonly property var colors: [Qt.rgba(47 / 255, 128 / 255, 237 / 255, 0.45), Qt.rgba(242 / 255, 140 / 255, 40 / 255, 0.45),
                                                   Qt.rgba(155 / 255, 81 / 255, 224 / 255, 0.45), Qt.rgba(39 / 255, 174 / 255, 160 / 255, 0.45),
                                                   Qt.rgba(86 / 255, 140 / 255, 220 / 255, 0.38)]
                    readonly property var solid: ["#2f80ed", "#f28c28", "#9b51e0", "#27aea0", "#568cdc"]
                    readonly property real span: Math.max(1, ctrl.viewEndMs - ctrl.viewStartMs)
                    Repeater {
                        model: ctrl.chapterBands
                        delegate: Rectangle {
                            required property var modelData
                            readonly property real from: Math.max(modelData.startMs, ctrl.viewStartMs)
                            readonly property real to: Math.min(modelData.endMs, ctrl.viewEndMs)
                            readonly property bool current: ctrl.playheadMs >= modelData.startMs && ctrl.playheadMs < modelData.endMs
                            visible: modelData.endMs > ctrl.viewStartMs && modelData.startMs < ctrl.viewEndMs
                            x: (from - ctrl.viewStartMs) / chapterStrip.span * chapterStrip.width
                            width: Math.max(chapterStrip.width * 0.005, (to - from) / chapterStrip.span * chapterStrip.width)
                            y: 2; height: 26; radius: 7
                            color: chapterStrip.colors[modelData.index % 5]
                            border.width: 1; border.color: current ? "white" : "transparent"
                            Text {
                                textFormat: Text.PlainText
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 4
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                text: modelData.name; color: parent.current ? "white" : "#e9f4ff"
                                font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            MouseArea {
                                anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onPositionChanged: (m) => page.showTip(parent, m.x, m.y, modelData.tip)
                                onExited: chartTip.visible = false
                                onClicked: page.seekTo(modelData.startMs + ctrl.offsetMs)
                            }
                            // Scene marker (0.7 pb-scene-mark): a diamond at the chapter start.
                            Rectangle {
                                visible: modelData.startMs >= ctrl.viewStartMs
                                x: -5; anchors.verticalCenter: parent.verticalCenter
                                width: 10; height: 10; rotation: 45
                                color: chapterStrip.solid[modelData.index % 5]
                                border.width: 1; border.color: "white"
                            }
                        }
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                        visible: ctrl.chapterBands.length === 0
                        text: qsTr("No chapters yet — + adds a scene marker at the playhead.")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    // "+" (0.7 pb-scene-add): a scene marker at the playhead.
                    Rectangle {
                        id: sceneAdd
                        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        width: 26; height: 26; radius: 13
                        color: addArea.containsMouse ? Qt.rgba(0, 217 / 255, 244 / 255, 0.3) : "#0a2340"
                        border.width: 1; border.color: Theme.cyan
                        Icon { anchors.centerIn: parent; name: "plus"; color: Theme.cyan; width: 14; height: 14 }
                        MouseArea {
                            id: addArea
                            anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onPositionChanged: (m) => page.showTip(sceneAdd, m.x, m.y, qsTr("Add a scene marker at the playhead (starts a chapter)"))
                            onExited: chartTip.visible = false
                            onClicked: ctrl.addSceneMarker(ctrl.playheadMs + ctrl.offsetMs)
                        }
                    }
                }
                Item { width: 1; height: 4; visible: heatBar.visible }
                // Heatmap: click = seek, drag = mark (0.7).
                HeatmapItem {
                    id: heatBar
                    width: parent.width; height: 18
                    opacity: 0.9
                    visible: !empty
                    viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                    markerStartMs: dragArea.dragging ? Math.min(dragArea.fromMs, dragArea.toMs) : ctrl.markerStartMs
                    markerEndMs: dragArea.dragging ? Math.max(dragArea.fromMs, dragArea.toMs) : ctrl.markerEndMs
                    Component.onCompleted: ctrl.attachHeatmap(heatBar)
                    MouseArea {
                        id: dragArea
                        anchors.fill: parent
                        hoverEnabled: true
                        property real fromMs: 0
                        property real toMs: 0
                        property bool dragging: false
                        function msAt(x) { return ctrl.msAtFraction(x / width) }
                        onPressed: (m) => { fromMs = msAt(m.x); toMs = fromMs; dragging = false }
                        onPositionChanged: (m) => {
                            if (pressed) {
                                toMs = msAt(m.x)
                                dragging = Math.abs(toMs - fromMs) > 150
                                chartTip.visible = false
                            } else {
                                page.showTip(heatBar, m.x, m.y, ctrl.heatTooltip(msAt(m.x)))
                            }
                        }
                        onReleased: (m) => { dragging = false; page.seekTo(ctrl.finishHeatmapDrag(fromMs, msAt(m.x))) }
                        onExited: chartTip.visible = false
                    }
                }
                Row {
                    id: markRow
                    height: 34; spacing: 10
                    visible: ctrl.hasScript
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: ctrl.markerLabel
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    FlowButton {
                        anchors.verticalCenter: parent.verticalCenter; implicitHeight: 28
                        text: qsTr("Zoom selection"); enabled: ctrl.hasMarker
                        onClicked: ctrl.zoomToSelection()
                    }
                    FlowButton {
                        anchors.verticalCenter: parent.verticalCenter; implicitHeight: 28
                        text: qsTr("Clear selection"); enabled: ctrl.hasMarker
                        onClicked: ctrl.clearMarker()
                    }
                    // Zoom (0.7 zoom row): wheel on the curve, buttons around the playhead.
                    IconButton { anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28; icon: "minus"; tip: qsTr("Zoom out"); onClicked: ctrl.zoomBy(1 / 1.6) }
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: ctrl.zoomLabel; color: ctrl.zoomed ? Theme.cyan : Theme.textDim
                        font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    IconButton { anchors.verticalCenter: parent.verticalCenter; width: 28; height: 28; icon: "plus"; tip: qsTr("Zoom in"); onClicked: ctrl.zoomBy(1.6) }
                    FlowButton { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Reset zoom"); visible: ctrl.zoomed; implicitHeight: 28; onClicked: ctrl.resetZoom() }
                }
            }
            // Chapters (0.7 bottom tab): add from the marked range, seek, rename, remove.
            Item {
                visible: bottomTabs.current === 1
                x: 16; y: 46; width: parent.width - 32; height: parent.height - 58
                Row {
                    id: chapterAdd
                    spacing: 8
                    FlowTextField { id: chapterName; width: 220; placeholder: qsTr("Chapter name"); onAccepted: addChapter.clicked() }
                    FlowButton {
                        id: addChapter
                        text: qsTr("Add from selection"); enabled: ctrl.hasMarker
                        onClicked: if (ctrl.addChapterFromSelection(chapterName.text)) chapterName.text = ""
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: ctrl.markerLabel
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                }
                MarkList {
                    anchors.top: chapterAdd.bottom; anchors.topMargin: 10
                    width: parent.width; height: parent.height - chapterAdd.height - 10
                    model: ctrl.chapters
                    emptyText: qsTr("No chapters yet. Mark a range on the heatmap, then add it.")
                    onSeek: (i) => page.seekTo(ctrl.chapterVideoMs(i))
                    onRename: (i, name) => ctrl.renameChapter(i, name)
                    onRemove: (i) => ctrl.removeChapter(i)
                }
            }
            // Bookmarks (0.7 bottom tab).
            Item {
                visible: bottomTabs.current === 2
                x: 16; y: 46; width: parent.width - 32; height: parent.height - 58
                Row {
                    id: bookmarkAdd
                    spacing: 8
                    FlowTextField { id: bookmarkName; width: 220; placeholder: qsTr("Bookmark"); onAccepted: addBookmark.clicked() }
                    FlowButton {
                        id: addBookmark
                        text: qsTr("Add at playhead"); enabled: ctrl.hasScript
                        onClicked: if (ctrl.addBookmark(bookmarkName.text, media.position)) bookmarkName.text = ""
                    }
                }
                MarkList {
                    anchors.top: bookmarkAdd.bottom; anchors.topMargin: 10
                    width: parent.width; height: parent.height - bookmarkAdd.height - 10
                    model: ctrl.bookmarks
                    emptyText: qsTr("No bookmarks yet.")
                    onSeek: (i) => page.seekTo(ctrl.bookmarkVideoMs(i))
                    onRename: (i, name) => ctrl.renameBookmark(i, name)
                    onRemove: (i) => ctrl.removeBookmark(i)
                }
            }
            // Scene Map comes as its own mode (P4); said plainly, no fake view.
            Item {
                visible: bottomTabs.current === 3
                x: 16; y: 46; width: parent.width - 32; height: parent.height - 58
                Text {
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    text: qsTr("Scene Map (scenes, thumbnails, rhythm) comes with the Scene Map mode in P4.")
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 13
                }
            }
            // Audio Analysis (0.7): the whole video's sound with the beat grid; click seeks.
            Item {
                visible: bottomTabs.current === 4
                x: 16; y: 46; width: parent.width - 32; height: parent.height - 58
                Row {
                    id: bpmRow
                    spacing: 10
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: ctrl.bpm > 0 ? qsTr("%1 BPM").arg(ctrl.bpm) : qsTr("— BPM")
                        color: Theme.text; font.family: Theme.font; font.pixelSize: 14
                    }
                    FlowTextField {
                        width: 90; placeholder: qsTr("BPM")
                        onAccepted: ctrl.bpm = parseFloat(text) || 0
                    }
                    FlowButton {
                        text: qsTr("BPM grid"); checked: ctrl.bpmGrid
                        onClicked: ctrl.bpmGrid = !ctrl.bpmGrid
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Beat detection from the sound comes with the audio tempo port (P3).")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                }
                AudioLevelItem {
                    id: wholeAudio
                    anchors.top: bpmRow.bottom; anchors.topMargin: 10
                    width: parent.width; height: 72
                    source: ctrl.videoUrl
                    viewStartMs: 0; viewEndMs: media.duration > 0 ? media.duration : -1
                    playheadMs: media.position
                    label: ""
                    MouseArea {
                        anchors.fill: parent
                        onClicked: (m) => page.seekTo(Math.round(m.x / width * media.duration))
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    anchors.centerIn: wholeAudio
                    visible: wholeAudio.empty
                    text: wholeAudio.loading ? qsTr("Reading the sound…") : qsTr("Link a video to see its sound.")
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 13
                }
            }
        }

    }

    // Shared chart tooltip (curve, heatmap).
    Rectangle {
        id: chartTip
        visible: false; z: 100
        width: tipText.implicitWidth + 16; height: tipText.implicitHeight + 10
        radius: 8; color: "#e60a1a2e"; border.color: Theme.cardBorder
        Text { textFormat: Text.PlainText; id: tipText; anchors.centerIn: parent; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
    }

    // Drop a video or a script anywhere on the page.
    DropArea {
        anchors.fill: parent
        onDropped: (drop) => { if (drop.hasUrls) ctrl.openPaths(drop.urls.map(u => u.toString())) }
    }

    // Keys as 0.7 (Player, not while typing).
    Shortcut { sequence: "Space"; onActivated: page.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: page.seekBy(-5000) }
    Shortcut { sequence: "Right"; onActivated: page.seekBy(5000) }
    Shortcut { sequence: "Shift+Left"; onActivated: page.seekBy(-1000) }
    Shortcut { sequence: "Shift+Right"; onActivated: page.seekBy(1000) }
    Shortcut { sequence: ","; onActivated: page.seekBy(-200) }
    Shortcut { sequence: "."; onActivated: page.seekBy(200) }
    Shortcut { sequences: ["+", "="]; onActivated: ctrl.offsetMs = ctrl.offsetMs + 10 }
    Shortcut { sequence: "-"; onActivated: ctrl.offsetMs = ctrl.offsetMs - 10 }
    Shortcut { sequence: "L"; onActivated: ctrl.loopEnabled = !ctrl.loopEnabled }
    Shortcut { sequence: "A"; onActivated: ctrl.setLoopStart(media.position) }
    Shortcut { sequence: "B"; onActivated: ctrl.setLoopEnd(media.position) }
    Repeater {
        model: 9
        delegate: Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                enabled: !deckOpen
                onActivated: page.seekTo(ctrl.jumpTenthVideoMs(index + 1))
            }
        }
    }
    Shortcut { sequence: StandardKey.Open; onActivated: page.openFile() }
    Shortcut { sequence: "Esc"; enabled: page.fullStage; onActivated: page.fullStage = false }
}
