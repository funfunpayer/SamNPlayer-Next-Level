import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import SamN

// Player mode: the video card with transport and heatmap, and the timeline
// card with the curve, the channel lanes and the audio level lane.
// All logic lives in PlayerController (C++); this file only shows it.
Item {
    id: page
    property alias controller: ctrl

    property real speed: 1.0
    // Nothing open yet: round Play + Open… (D-138 / 0.8 #550), not the old welcome stage.
    readonly property bool empty: !ctrl.hasScript && ctrl.videoName === ""
    // Video fullscreen (0.7: button, double-click, Esc leaves).
    property bool fullStage: false
    onFullStageChanged: {
        const w = page.Window.window
        if (w) w.visibility = fullStage ? Window.FullScreen : Window.Windowed
    }
    // Theatre (D-144): the video in the window without the side panels; the
    // curve strip under it stays and can be switched off. Remembered.
    property bool theatre: ctrl.uiValue("player.theatre", false)
    onTheatreChanged: ctrl.setUiValue("player.theatre", theatre)
    property bool theatreLanes: ctrl.uiValue("player.theatreLanes", true)
    onTheatreLanesChanged: ctrl.setUiValue("player.theatreLanes", theatreLanes)
    // Side panels away: fullscreen or theatre.
    readonly property bool bare: fullStage || theatre
    property bool muted: false
    readonly property string videoGlobs: Theme.videoGlobs
    // An overlay (Settings) is open: its keys win.
    property bool overlayOpen: false

    // The device connection of the app (Main.qml); the Player drives it.
    required property DeviceController device
    // The Library lists, shared with the Generator: Main.qml hands in the one
    // instance (`changed` only fires where something was written). Standalone,
    // the page has its own.
    property LibraryController library: ownLibrary
    // "Create script" for a video without one (0.7 open_media.js): Main.qml
    // opens the Generator with the video; `start` runs it right away.
    signal createRequested(url video, bool start)
    // "From the beat bar" on the no-script card (Cock Hero / Fap Hero).
    signal beatBarRequested(url video)
    signal editRequested(string script, url video, bool scenes)
    // The last script that was open, for "Back to the script" (0.7).
    property string lastScript: ""
    property url lastVideo
    property url autoCreatedFor  // once per video, scriptChanged comes more than once
    Connections {
        target: ctrl
        function onScriptChanged() {
            if (ctrl.hasScript) {
                page.lastScript = ctrl.scriptPath; page.lastVideo = ctrl.videoUrl
            } else if (ctrl.noScriptForVideo && ctrl.autoCreate && ctrl.videoUrl !== page.autoCreatedFor) {
                page.autoCreatedFor = ctrl.videoUrl
                page.createRequested(ctrl.videoUrl, true)
            }
        }
    }

    PlayerController {
        id: ctrl
        device: page.device
        onSleepReached: page.player.pause()
        // The Emergency stop pauses the video too (the device is already stopped).
        onEmergencyStopped: page.player.pause()
    }
    // A script without a video plays on its own clock (0.7 "Feel without
    // film"); `player` is whichever drives the playhead.
    readonly property bool scriptOnly: ctrl.hasScript && ctrl.videoName === ""
    readonly property QtObject player: scriptOnly ? clock : media
    onScriptOnlyChanged: {
        // A clock that was driving the output ends that playback here: its
        // own handler below no longer reports once the page is not script-only.
        if (clock.playbackState !== ScriptClock.StoppedState) ctrl.setPlaying(false)
        clock.stop()  // also back to 0
    }
    // fromVideo: a report of the video's media player (the output then
    // follows the playhead estimated between the reports).
    function playheadMoved(pos, fromVideo) {
        if (fromVideo) ctrl.reportMediaPosition(pos)
        else ctrl.setVideoPositionMs(pos)
        const jump = ctrl.loopJumpFor(pos)
        if (jump >= 0) page.player.position = jump
    }
    ScriptClock {
        id: clock
        objectName: "scriptClock"
        duration: page.scriptOnly ? ctrl.scriptDurationMs : 0
        playbackRate: page.speed
        onDurationChanged: if (page.scriptOnly) ctrl.setVideoDurationMs(clock.duration)
        onPlaybackStateChanged: if (page.scriptOnly) ctrl.setPlaying(clock.playbackState === ScriptClock.PlayingState)
        onPositionChanged: if (page.scriptOnly) page.playheadMoved(clock.position)
        // Play list: at the end, Auto-advance opens the next script; one
        // without a video starts on its own (0.7).
        onFinished: {
            if (ctrl.playlistFinished()) {
                if (ctrl.videoName !== "") media.playWhenLoaded = true
                else Qt.callLater(clock.play)
            }
        }
    }
    Binding { target: ctrl.output; property: "speed"; value: page.speed }

    // A video whose picture never comes: the sound and the clock run, but
    // no frame reaches the screen (Owner 26 Oct: AV1 on a GPU without an
    // AV1 decoder, where Qt's FFmpeg has no software decoder and reports no
    // error). After 1.5 s of playing without a frame the stage says so and
    // offers an H.264 copy.
    property bool frameSeen: false
    property bool noPicture: false
    property bool convertWhenReady: false
    Connections {
        objectName: "frameWatch"
        target: videoOut.videoSink
        function onVideoFrameChanged() {
            if (!page.frameSeen) ctrl.reportFirstPicture(media.metaData.stringValue(MediaMetaData.VideoCodec))
            page.frameSeen = true
        }
    }
    // Plays the open video as soon as it is loaded (the package smoke test).
    function playWhenReady() {
        if (media.mediaStatus === MediaPlayer.LoadedMedia || media.mediaStatus === MediaPlayer.BufferedMedia) media.play()
        else media.playWhenLoaded = true
    }
    Timer {
        objectName: "noPictureWatch"
        interval: 500; repeat: true
        running: !page.frameSeen && !page.noPicture && media.hasVideo && media.playbackState === MediaPlayer.PlayingState
        onTriggered: if (media.position >= 1500 && !page.frameSeen) {
            page.noPicture = true
            ctrl.reportNoPicture(media.metaData.stringValue(MediaMetaData.VideoCodec))
        }
    }
    ClipPrepController {
        id: convertCtl
        onConverted: (path) => ctrl.openPath(path)
        onToolChanged: if (page.convertWhenReady && ffmpegReady) {
            page.convertWhenReady = false
            page.convertVideo()
        }
    }
    // Duration for convert: Qt may report 0 on FormatError; fall back to the
    // controller's last known length (script / prior metadata).
    function convertDurationS() {
        const ms = Math.max(media.duration, ctrl.videoDurationMs)
        return ms > 0 ? ms / 1000 : 0
    }
    function convertVideo() {
        media.pause()
        const secs = page.convertDurationS()
        if (secs <= 0) {
            ctrl.reportMediaError(MediaPlayer.FormatError,
                                  qsTr("unknown length — open a playable copy first, or use Clip prep"))
            return
        }
        if (convertCtl.ffmpegReady) convertCtl.convertForPlayback(ctrl.localPath(media.source.toString()), secs)
        else { page.convertWhenReady = true; convertCtl.fetchFfmpeg() }
    }

    MediaPlayer {
        id: media
        objectName: "media"
        source: ctrl.videoUrl
        videoOutput: videoOut
        onDurationChanged: ctrl.setVideoDurationMs(media.duration)
        audioOutput: AudioOutput { id: audio; volume: transport.volume; muted: page.muted }
        playbackRate: page.speed
        onPlaybackStateChanged: ctrl.setMediaPlaying(playbackState === MediaPlayer.PlayingState)
        // errorChanged, not errorOccurred: the FFmpeg backend of Qt 6.4 sets
        // the error without emitting errorOccurred.
        onErrorChanged: if (media.error !== MediaPlayer.NoError) ctrl.reportMediaError(media.error, media.errorString)
        // Play list: at the end of the video, Auto-advance opens the next
        // script and it starts once its video is loaded (0.7).
        property bool playWhenLoaded: false
        // A seek before the video is loaded (a project's saved position)
        // waits here; clamped to a duration of 0 it would land at 0.
        property real pendingSeekMs: -1
        onSourceChanged: { pendingSeekMs = -1; page.frameSeen = false; page.noPicture = false }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && pendingSeekMs >= 0) {
                const ms = pendingSeekMs
                pendingSeekMs = -1
                page.seekTo(ms)
            }
            if (mediaStatus === MediaPlayer.EndOfMedia && ctrl.playlistFinished()) {
                if (ctrl.videoName !== "") playWhenLoaded = true
                else Qt.callLater(clock.play)  // the next script has no video
            }
            else if (mediaStatus === MediaPlayer.LoadedMedia && playWhenLoaded) { playWhenLoaded = false; media.play() }
        }
        onPositionChanged: page.playheadMoved(media.position, true)
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open video or script")
        // 0.7 PickMediaFile: one dialog, the extensions of VIDEO_EXTS.
        nameFilters: [qsTr("Video or script (%1 *.samn *.funscript)").arg(page.videoGlobs), qsTr("Videos (%1)").arg(page.videoGlobs),
                      qsTr("Scripts (*.samn *.funscript)"), qsTr("All files (*)")]
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
        nameFilters: [qsTr("Videos (%1)").arg(page.videoGlobs), qsTr("All files (*)")]
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
    // The top-bar device chip: Inspector open on Devices.
    function showDevices() { inspector.collapsed = false; inspector.tab = 0 }
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    function togglePlay() { playing ? player.pause() : player.play() }
    function setVolume(v) {
        transport.volume = Math.max(0, Math.min(1, Math.round(v * 20) / 20))  // steps of 0.05 (0.7)
        ctrl.setUiValue("player.volume", transport.volume)
    }
    function stop() { player.stop(); if (!scriptOnly) media.position = 0 }
    function previous() {
        const wasPlaying = playing
        if (ctrl.playlistHasPrevious) {
            player.stop()
        }
        if (ctrl.playlistPrevious()) {
            if (wasPlaying) {
                if (ctrl.videoName !== "") media.playWhenLoaded = true
                else Qt.callLater(clock.play)
            }
        } else {
            seekTo(0)
        }
    }
    // A seek of the video first freezes the device's playhead estimate,
    // which then takes the seek's position report as it is. A seek to where
    // the video already is gives no report, so it announces nothing.
    function seekBy(ms) {
        const to = Math.max(0, Math.min(player.duration, player.position + ms))
        if (!scriptOnly && to !== player.position) ctrl.holdMediaPlayhead()
        player.position = to
    }
    function seekTo(ms) {
        if (ms < 0) return
        if (scriptOnly) clock.position = ms
        else if (media.duration <= 0 || media.mediaStatus === MediaPlayer.LoadingMedia) media.pendingSeekMs = ms
        else {
            const to = Math.max(0, Math.min(media.duration, ms))
            if (to !== media.position) ctrl.holdMediaPlayhead()
            media.position = to
        }
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
    LibraryController { id: ownLibrary }
    Connections {
        target: ctrl
        function onScriptOpened(path) { page.library.noteScriptOpened(path) }
        function onVideoLinked(path) { page.library.noteVideoOpened(path) }
        function onScriptEdited(path) { page.library.noteScriptEdited(path) }
    }
    LibraryPanel {
        id: libraryPanel
        objectName: "libraryPanel"
        visible: !page.bare
        anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
        library: page.library
        currentPath: ctrl.scriptPath
        collapsed: ctrl.uiValue("player.libraryCollapsed", false)
        onCollapsedChanged: ctrl.setUiValue("player.libraryCollapsed", collapsed)
        onOpenFile: page.openFile()
        onOpenPath: (path) => ctrl.openPath(path)
        onShowAudioAnalysis: timeline.showAudioAnalysis()
        onShowBpm: { inspector.tab = 1; ctrl.setUiValue("player.inspectorTab", 1) }
    }
    InspectorPanel {
        id: inspector
        objectName: "inspector"
        visible: !page.bare
        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
        ctrl: ctrl
        device: page.device
        collapsed: ctrl.uiValue("player.inspectorCollapsed", false)
        onCollapsedChanged: ctrl.setUiValue("player.inspectorCollapsed", collapsed)
        onSeekRequested: (ms) => page.seekTo(ms)
        onAddToListRequested: addListDialog.open()
        videoPositionMs: page.player.position
        onProjectLoadRequested: projectDialog.open()
        onEditRequested: (scenes) => page.editRequested(ctrl.scriptPath, ctrl.videoUrl, scenes)
    }
    Item {
        id: center
        anchors.left: page.bare ? parent.left : libraryPanel.right; anchors.leftMargin: page.bare ? 0 : 14
        anchors.right: page.bare ? parent.right : inspector.left; anchors.rightMargin: page.bare ? 0 : 14
        anchors.top: parent.top; anchors.bottom: parent.bottom

        // Header: what is open, the script variant, Open.
        PlayerHeader {
            id: header
            visible: !page.fullStage
            width: parent.width; height: 40
            ctrl: ctrl
            empty: page.empty
            theatre: page.theatre; theatreLanes: page.theatreLanes; fullStage: page.fullStage
            onOpenRequested: page.openFile()
            onVideoPickRequested: videoDialog.open()
            onBookmarkRequested: ctrl.addBookmark("", page.player.position)
            onSnapshotRequested: {
                snapshotDialog.currentFile = ctrl.snapshotFolder() + "/" + ctrl.snapshotName(media.position)
                snapshotDialog.open()
            }
            onTheatreToggled: page.theatre = !page.theatre
            onTheatreLanesToggled: page.theatreLanes = !page.theatreLanes
            onFullStageToggled: page.fullStage = !page.fullStage
        }

        // Link warning and the error bar. The component owns their height.
        PlayerStatusBars {
            id: statusBars
            anchors.top: header.bottom
            width: parent.width
            ctrl: ctrl
            noPicture: page.noPicture
            convertCtl: convertCtl
            onConvertRequested: page.convertVideo()
            onDismissLinkWarningRequested: ctrl.dismissVideoLinkWarning()
        }

        // Empty state: round Play + Open…, centred.
        PlayerEmptyHome {
            anchors.fill: parent
            empty: page.empty
            onOpenRequested: page.openFile()
        }
        // Video card: stage + transport.
        GlassCard {
            id: videoCard
            objectName: "videoCard"
            visible: !page.empty
            anchors.top: page.fullStage ? parent.top : statusBars.bottom; anchors.topMargin: page.fullStage ? 0 : 10
            width: parent.width
            height: page.fullStage ? parent.height
                    : page.theatre ? (page.theatreLanes ? Math.max(300, (parent.height - header.height) * 0.76) : parent.height - header.height - 10)
                    : Math.max(300, (parent.height - header.height) * 0.58)
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
                SilkLayer { running: stage.visible && Qt.application.state === Qt.ApplicationActive && !Theme.reducedMotion }
                VideoOutput {
                    id: videoOut
                    anchors.fill: parent
                    visible: ctrl.videoName !== ""
                }
                Rectangle {
                    objectName: "noPictureCard"
                    visible: page.noPicture && ctrl.videoName !== ""
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 32, 460); height: noPicCol.implicitHeight + 28
                    radius: Theme.radiusSmall
                    color: Qt.rgba(0.05, 0.07, 0.10, 0.92); border.color: Qt.rgba(1, 0.4, 0.35, 0.5)
                    Column {
                        id: noPicCol
                        anchors.centerIn: parent; width: parent.width - 28; spacing: 10
                        Text {
                            textFormat: Text.PlainText
                            width: parent.width; wrapMode: Text.Wrap
                            text: qsTr("No picture: this PC cannot decode the video. Sound and script play on.")
                            color: "#ffd0c8"; font.family: Theme.fontSoft; font.pixelSize: 14
                        }
                        Text {
                            objectName: "noPictureStatus"
                            textFormat: Text.PlainText
                            width: parent.width; wrapMode: Text.Wrap
                            visible: text !== ""
                            text: convertCtl.cutting ? convertCtl.status + "  " + convertCtl.progress + " %"
                                  : convertCtl.fetching ? convertCtl.toolStatus : convertCtl.status
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        Row {
                            spacing: 8
                            FlowButton {
                                objectName: "convertVideo"
                                primary: true
                                enabled: !convertCtl.cutting && !convertCtl.fetching
                                text: qsTr("Make playable")
                                onClicked: page.convertVideo()
                            }
                            FlowButton {
                                objectName: "cancelConvert"
                                visible: convertCtl.cutting
                                text: qsTr("Stop")
                                onClicked: convertCtl.cancel()
                            }
                        }
                    }
                }
                // MT-Debug trajectory lines (0.7 playback.js): tip accent +
                // partner teal + moving tip dot, aligned to the letterboxed video.
                Item {
                    id: trajLayer
                    objectName: "trajectoryLayer"
                    readonly property rect area: videoOut.contentRect
                    readonly property var m: ctrl.contactMarks
                    readonly property size res: media.metaData.value(MediaMetaData.Resolution) || Qt.size(0, 0)
                    readonly property real srcW: res.width > 0 ? res.width : (m.width || 0)
                    readonly property real srcH: res.height > 0 ? res.height : (m.height || 0)
                    readonly property var tipNow: ctrl.tipAt(ctrl.playheadMs)
                    visible: ctrl.trajectoryOverlay && ctrl.hasTrajectory && ctrl.videoName !== "" && srcW > 0 && srcH > 0
                    x: area.x; y: area.y; width: area.width; height: area.height
                    Canvas {
                        id: trajCanvas
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.clearRect(0, 0, width, height)
                            if (!trajLayer.visible) return
                            function drawPath(pts, color, widthPx) {
                                if (!pts || pts.length < 2) return
                                ctx.beginPath()
                                ctx.strokeStyle = color
                                ctx.lineWidth = widthPx
                                ctx.lineJoin = "round"
                                ctx.lineCap = "round"
                                for (let i = 0; i < pts.length; ++i) {
                                    const x = pts[i].x / trajLayer.srcW * width
                                    const y = pts[i].y / trajLayer.srcH * height
                                    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.stroke()
                            }
                            drawPath(ctrl.partnerPathPoints(), "#3dccc0", 2.0)
                            drawPath(ctrl.tipPathPoints(), Theme.orange, 2.2)
                        }
                        Connections {
                            target: ctrl
                            function onScriptChanged() { trajCanvas.requestPaint() }
                            function onViewOptionsChanged() { trajCanvas.requestPaint() }
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                    }
                    Rectangle {
                        objectName: "trajectoryTipDot"
                        visible: trajLayer.tipNow.x !== undefined
                        width: 10; height: 10; radius: 5
                        x: (trajLayer.tipNow.x || 0) / trajLayer.srcW * trajLayer.width - width / 2
                        y: (trajLayer.tipNow.y || 0) / trajLayer.srcH * trajLayer.height - height / 2
                        color: Theme.orange
                        border.color: "white"; border.width: 1.5
                    }
                }
                // The script's contact marks on the picture (0.7 playback.js
                // contact overlay): tip blue on the tip path; gold stays put
                // when fixed else follows partner; magenta extras follow their
                // own path (track_multi), else partner for old scripts.
                Item {
                    id: marksLayer
                    objectName: "contactMarksLayer"
                    readonly property var m: ctrl.contactMarks
                    readonly property rect area: videoOut.contentRect
                    // Video pixels: the frame size from the media, or the tip path's frame.
                    readonly property size res: media.metaData.value(MediaMetaData.Resolution) || Qt.size(0, 0)
                    readonly property real srcW: res.width > 0 ? res.width : (m.width || 0)
                    readonly property real srcH: res.height > 0 ? res.height : (m.height || 0)
                    readonly property var tipNow: m.tip ? ctrl.tipAt(ctrl.playheadMs) : ({})
                    readonly property var partnerNow: m.hasPartner ? ctrl.partnerAt(ctrl.playheadMs) : ({})
                    visible: ctrl.showContactMarks && ctrl.videoName !== "" && m.hint !== undefined && srcW > 0 && srcH > 0
                    x: area.x; y: area.y; width: area.width; height: area.height
                    function followPartner(box) {
                        const b = Object.assign({}, box)
                        if (!b.fixed && partnerNow.x !== undefined) {
                            b.x = partnerNow.x - b.w / 2
                            b.y = partnerNow.y - b.h / 2
                        }
                        return b
                    }
                    function followExtra(box, index) {
                        const b = Object.assign({}, box)
                        if (b.fixed) return b
                        const own = m.hasExtras ? ctrl.extraAt(index, ctrl.playheadMs) : ({})
                        if (own.x !== undefined) {
                            b.x = own.x - b.w / 2
                            b.y = own.y - b.h / 2
                        } else if (partnerNow.x !== undefined) {
                            // Old scripts: extras followed the partner path.
                            b.x = partnerNow.x - b.w / 2
                            b.y = partnerNow.y - b.h / 2
                        }
                        return b
                    }
                    function boxes() {
                        const out = []
                        if (m.tip) {
                            const b = Object.assign({}, m.tip)
                            // The tip is tracked: centre the box on the path.
                            if (tipNow.x !== undefined) { b.x = tipNow.x - b.w / 2; b.y = tipNow.y - b.h / 2 }
                            out.push({ b: b, color: "#50b4ff", label: b.cls || m.tipClass || qsTr("tip") })
                        }
                        if (m.primary)
                            out.push({ b: followPartner(m.primary), color: "#f2b03d", label: m.primary.cls || qsTr("contact") })
                        for (let i = 0; i < (m.extras || []).length; ++i)
                            out.push({ b: followExtra(m.extras[i], i), color: "#e070a0", label: m.extras[i].cls || qsTr("extra %1").arg(i + 1) })
                        return out
                    }
                    Repeater {
                        model: marksLayer.visible ? marksLayer.boxes() : []
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            objectName: "contactMark_" + index
                            x: modelData.b.x / marksLayer.srcW * marksLayer.width
                            y: modelData.b.y / marksLayer.srcH * marksLayer.height
                            width: modelData.b.w / marksLayer.srcW * marksLayer.width
                            height: modelData.b.h / marksLayer.srcH * marksLayer.height
                            color: "transparent"; border.color: modelData.color; border.width: 2
                            Text {
                                textFormat: Text.PlainText
                                x: 3; y: -height - 2
                                text: parent.modelData.label
                                color: parent.modelData.color; font.family: Theme.fontSoft; font.pixelSize: 11
                            }
                        }
                    }
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
                // Feel without film (D-138): Flow idle graphic + copy + Link video.
                // Empty home stays Play + Open only — this panel is script, no video.
                Column {
                    id: noVideoHint
                    objectName: "feelWithoutFilm"
                    anchors.centerIn: parent
                    visible: page.scriptOnly
                    spacing: 12
                    opacity: 0.96
                    width: Math.min(parent.width - 48, 480)
                    FlowStage {
                        id: feelFlow
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, 420)
                        height: width * 236 / 420
                        // scriptOnly only: the clock owns pause/idle here.
                        stageState: page.playing ? "playing"
                                    : (clock.playbackState === ScriptClock.PausedState ? "paused" : "idle")
                    }
                    Text {
                        objectName: "feelWithoutFilmTitle"
                        textFormat: Text.PlainText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Playing without video")
                        color: Theme.text
                        font.family: Theme.fontSoft
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }
                    Text {
                        textFormat: Text.PlainText
                        width: parent.width
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Device and curve alone — video is optional. Play starts immediately.")
                        color: Theme.textDim
                        font.family: Theme.fontSoft
                        font.pixelSize: 13
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 10
                        FlowButton {
                            objectName: "feelWithoutFilmPlay"
                            primary: true
                            text: page.playing ? qsTr("Pause") : qsTr("Play")
                            onClicked: page.togglePlay()
                        }
                        FlowButton {
                            objectName: "feelWithoutFilmLink"
                            text: qsTr("Link video…")
                            onClicked: videoDialog.open()
                        }
                    }
                }
                // Video first: a video without a script says so and offers the ways on (0.7 open_media.js).
                Rectangle {
                    objectName: "noScriptCard"
                    anchors.centerIn: parent
                    z: 2  // over the time label
                    visible: ctrl.noScriptForVideo
                    width: Math.min(parent.width - 40, 520)
                    // A small window: the card fits the stage and scrolls.
                    height: Math.min(noScript.implicitHeight + 40, parent.height - 16)
                    radius: Theme.radius
                    color: Qt.rgba(4 / 255, 14 / 255, 28 / 255, 0.92); border.color: Theme.cardBorder
                    Flickable {
                        anchors.fill: parent; anchors.margins: 20
                        clip: true
                        contentHeight: noScript.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: noScript
                        width: parent.width
                        spacing: 12
                        Row {
                            spacing: 14
                            width: parent.width
                            // 0.7: a still frame 3 s in.
                            Rectangle {
                                width: 128; height: 72; radius: 8; color: "black"; clip: true
                                border.color: Theme.cardBorder
                                Image {
                                    objectName: "noScriptStill"
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    source: ctrl.noScriptForVideo && ctrl.videoPath !== "" ? "image://frames/3000/144/" + encodeURIComponent(ctrl.videoPath) : ""
                                }
                            }
                            Column {
                                width: parent.width - 142; spacing: 4
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("New video"); color: Theme.cyan
                                    font.family: Theme.fontSoft; font.pixelSize: 12; font.weight: Font.Medium
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    width: parent.width; elide: Text.ElideMiddle
                                    text: ctrl.videoName; color: Theme.text
                                    font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                                }
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            width: parent.width; wrapMode: Text.Wrap
                            text: qsTr("No script for this video yet. SamNPlayer can create one — in the Generator, with this video.")
                            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14
                        }
                        Text {
                            textFormat: Text.PlainText
                            width: parent.width; wrapMode: Text.Wrap
                            text: ctrl.scriptFolders.length
                                  ? qsTr("Searched beside the video and in %n script folder(s).", "", ctrl.scriptFolders.length)
                                  : qsTr("Searched beside the video. Add a script folder to search there too.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        Flow {
                            width: parent.width; spacing: 8
                            FlowButton {
                                objectName: "noScriptCreate"
                                primary: true; text: qsTr("Create script")
                                onClicked: page.createRequested(ctrl.videoUrl, false)
                            }
                            FlowButton {
                                objectName: "noScriptBeatBar"
                                text: qsTr("From the beat bar")
                                onClicked: page.beatBarRequested(ctrl.videoUrl)
                            }
                            FlowButton { objectName: "noScriptChoose"; text: qsTr("Choose script…"); onClicked: scriptDialog.open() }
                            FlowButton { objectName: "noScriptOpenOther"; text: qsTr("Open other…"); onClicked: openDialog.open() }
                            FlowButton { objectName: "noScriptAddFolder"; text: qsTr("Add a script folder…"); onClicked: folderDialog.open() }
                            FlowButton {
                                objectName: "noScriptBack"
                                visible: page.lastScript !== ""
                                text: qsTr("Back to the script")
                                onClicked: ctrl.openScript(page.lastScript, page.lastVideo.toString())
                            }
                        }
                        RowLayout {
                            width: parent.width; spacing: 10
                            FlowToggle {
                                objectName: "noScriptAutoCreate"
                                checked: ctrl.autoCreate
                                onToggled: (c) => ctrl.autoCreate = c
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                text: qsTr("Always create right away when a video has no script")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                        }
                    }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 14
                    text: ctrl.formatTime(page.player.position) + " / " + ctrl.formatTime(page.player.duration)
                    color: "white"; font.family: Theme.fontSoft; font.pixelSize: 13
                    visible: ctrl.videoName !== "" || page.scriptOnly
                }
                // 0.7 keys hint on the stage (playback.js chrome).
                Text {
                    id: keysHint
                    objectName: "keysHint"
                    textFormat: Text.PlainText
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 14
                    visible: !page.empty && !page.fullStage
                    opacity: 0.55
                    text: qsTr("Space play/pause · ←/→ seek · ,/. fine · +/- offset · E Extended-O · T theatre · Esc leave fullscreen")
                    color: "white"; font.family: Theme.fontSoft; font.pixelSize: 11
                }
            }
            VideoTransport {
                id: transport
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                position: page.player.position
                duration: page.player.duration
                playing: page.playing
                muted: page.muted
                volume: ctrl.uiValue("player.volume", 0.8)  // remembered (0.7 pb.volume)
                // Live channel values at the playhead (0.7 stat row).
                info: ctrl.hasScript ? qsTr("Vibration %1   ·   Suction %2").arg(ctrl.vibrationNow).arg(ctrl.suctionNow) : ""
                canPrevious: ctrl.hasScript || ctrl.videoName !== ""
                showNext: ctrl.playlistHasNext
                onSeekRequested: (ms) => page.seekTo(ms)
                onSeekByRequested: (ms) => page.seekBy(ms)
                onTogglePlayRequested: page.togglePlay()
                // Previous: the item before in the list (playing on if it
                // played), else back to the start (0.7).
                onPreviousRequested: page.previous()
                onStopRequested: page.stop()
                onNextRequested: {
                    const wasPlaying = page.playing
                    page.player.stop()
                    if (ctrl.playlistNext() && wasPlaying) {
                        if (ctrl.videoName !== "") media.playWhenLoaded = true
                        else Qt.callLater(clock.play)
                    }
                }
                onMuteToggled: page.muted = !page.muted
                onVolumeMoved: (v) => page.setVolume(v)
                // Left (0.7 transport): sleep timer, loop selection, speed.
                leftItems: [
                    FlowCombo {
                        width: 108
                        model: [qsTr("Sleep off"), "15 min", "30 min", "45 min", "60 min", "90 min"]
                        readonly property var minutes: [0, 15, 30, 45, 60, 90]
                        currentIndex: Math.max(0, minutes.indexOf(ctrl.sleepMinutes))
                        onActivated: (i) => ctrl.sleepMinutes = minutes[i]
                    },
                    FlowButton {
                        text: qsTr("Loop"); checked: ctrl.loopEnabled
                        onClicked: ctrl.loopEnabled = !ctrl.loopEnabled
                    },
                    FlowCombo {
                        width: 78
                        model: ["0.5×", "0.75×", "1.0×", "1.25×", "1.5×", "2.0×"]
                        readonly property var rates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                        currentIndex: rates.indexOf(page.speed)
                        onActivated: (i) => page.speed = rates[i]
                    },
                    // 0.7 Extended-O trigger (key E): shown while Extended-O is on,
                    // usable while playing and not already running.
                    // An icon, so the transport keeps its room (a text button pushed
                    // Stop under the volume at 1400 px).
                    IconButton {
                        objectName: "extendedOButton"
                        width: 36; height: 36
                        anchors.verticalCenter: parent.verticalCenter
                        visible: ctrl.output.extendedOEnabled
                        enabled: ctrl.output.canTriggerExtendedO
                        opacity: enabled ? 1 : 0.4
                        icon: "effects"; tip: qsTr("Extended-O (E)")
                        onClicked: ctrl.output.triggerExtendedO()
                    }
                ]
            }
        }

        PlayerTimeline {
            id: timeline
            visible: !page.empty && !page.fullStage && (!page.theatre || page.theatreLanes)
            anchors.top: videoCard.bottom; anchors.topMargin: 12
            anchors.bottom: parent.bottom
            width: parent.width
            ctrl: ctrl
            empty: page.empty
            mediaPosition: media.position
            mediaDuration: media.duration
            playheadPosition: page.player.position
            onSeekRequested: (ms) => page.seekTo(ms)
            onTipRequested: (item, x, y, text) => page.showTip(item, x, y, text)
        }

    }

    // Shared chart tooltip (curve, heatmap).
    Rectangle {
        id: chartTip
        visible: false; z: 100
        width: tipText.implicitWidth + 16; height: tipText.implicitHeight + 10
        radius: Theme.radiusSmall; color: "#e60a1a2e"; border.color: Theme.cardBorder
        Text { textFormat: Text.PlainText; id: tipText; anchors.centerIn: parent; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
    }

    // Drop a video or a script anywhere on the page.

    // Keys as 0.7 (Player, not while typing).
    Shortcut { sequence: "Space"; enabled: page.visible && !page.overlayOpen; onActivated: page.togglePlay() }
    Shortcut { sequence: "Left"; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(-5000) }
    Shortcut { sequence: "Right"; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(5000) }
    Shortcut { sequence: "Shift+Left"; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(-1000) }
    Shortcut { sequence: "Shift+Right"; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(1000) }
    Shortcut { sequence: ","; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(-200) }
    Shortcut { sequence: "."; enabled: page.visible && !page.overlayOpen; onActivated: page.seekBy(200) }
    Shortcut { sequences: ["+", "="]; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.offsetMs = ctrl.offsetMs + 10 }
    Shortcut { sequence: "-"; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.offsetMs = ctrl.offsetMs - 10 }
    Shortcut { sequence: "L"; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.loopEnabled = !ctrl.loopEnabled }
    Shortcut { sequence: "A"; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.setLoopStart(page.player.position) }
    Shortcut { sequence: "B"; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.setLoopEnd(page.player.position) }
    // 0.7 key E: Extended-O (ignored when not playing, off or running).
    Shortcut { sequence: "E"; enabled: page.visible && !page.overlayOpen; onActivated: ctrl.output.triggerExtendedO() }
    // 0.7 key O: a 4 s primary O-marker at the playhead.
    Shortcut { sequence: "O"; enabled: page.visible && !page.overlayOpen && !page.empty; onActivated: ctrl.addOMarkerAt(page.player.position) }
    Repeater {
        model: 9
        delegate: Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                enabled: page.visible && !page.overlayOpen
                onActivated: page.seekTo(ctrl.jumpTenthVideoMs(index + 1))
            }
        }
    }
    Shortcut { sequence: "Esc"; enabled: page.visible && page.fullStage && !page.overlayOpen; onActivated: page.fullStage = false }
    Shortcut { sequence: "T"; enabled: page.visible && !page.overlayOpen && !page.empty; onActivated: page.theatre = !page.theatre }
    // Curve-edit undo. Same keys as the Generator editor; this page is hidden
    // in other modes, and overlay (Settings) wins, like the keys above.
    Shortcut {
        sequences: [StandardKey.Undo]
        enabled: page.visible && !page.overlayOpen && ctrl.canUndo
        onActivated: ctrl.undo()
    }
    Shortcut {
        sequences: [StandardKey.Redo, "Ctrl+Y"]
        enabled: page.visible && !page.overlayOpen && ctrl.canRedo
        onActivated: ctrl.redo()
    }
}
