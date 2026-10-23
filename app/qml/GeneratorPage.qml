import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import SamN

// The Generator mode is the Editor (D-137), laid out like the Player (D-144):
// the video on top, the curves under it, the settings on the right. The
// video plays as in the Player, with the same transport (D-154); paused, the
// exact frame shows so the tip box can be drawn on it.
// Creating: 1 Video · 2 Tip · 3 Feel · 4 Create · 5 Review; the result opens
// here for editing (0.7 generator.js + playback.js edit tools).
Item {
    id: page
    property bool overlayOpen: false
    // Right panel: 0 create (video, feel, create), 1 scenes (D-145).
    property int sidePanel: 0
    property int selectedScene: -1
    // Scene colours c0-c4 (0.7 scene strip).
    readonly property var sceneColors: ["#3dccc0", "#f2b03d", "#e070a0", "#6b8cff", "#7ee787"]
    // The Player's video, for "Use Player video".
    property url playerVideoUrl
    property alias controller: gen
    property alias editor: edit
    // "Play in the Player": the Player plays, it does not edit (D-144).
    signal reviewRequested(string path)

    GeneratorController {
        id: gen
        // 0.8 "Optimize after Create" (D-156): the new script is optimized right away.
        onCreated: (path) => {
            library.noteScriptGenerated(path)
            if (edit.open(path) && gen.optimizeTarget !== "") edit.optimizeFor(gen.optimizeTarget)
        }
        onVideoChanged: if (gen.hasVideo) library.noteVideoOpened(gen.videoPath)
    }
    EditorController {
        id: edit
        durationMs: gen.durationMs
        videoPath: gen.videoPath
    }
    // The lists of the left column, shared with the Player's Library.
    LibraryController { id: library }
    Connections {
        target: edit
        function onHistoryChanged() { if (edit.hasScript && edit.canUndo) library.noteScriptEdited(edit.path) }
    }

    // Playback (D-154). Paused, the playhead is the decoded preview frame;
    // playing, it is the video's position.
    MediaPlayer {
        id: media
        objectName: "generatorMedia"
        source: gen.hasVideo ? gen.videoUrl : ""
        videoOutput: videoOut
        playbackRate: page.speed
        audioOutput: AudioOutput { volume: transport.volume; muted: page.muted }
        onPlaybackStateChanged: if (playbackState !== MediaPlayer.PlayingState && gen.hasVideo) gen.seekPreview(position)
    }
    readonly property bool playing: media.playbackState === MediaPlayer.PlayingState
    readonly property real playheadMs: playing ? media.position : gen.previewMs
    property real speed: 1.0
    property bool muted: false
    function togglePlay() {
        if (!gen.hasVideo) { videoDialog.open(); return }
        if (playing) { media.pause(); return }
        media.position = gen.previewMs
        media.play()
    }
    function seekTo(ms) {
        const t = Math.max(0, Math.min(gen.durationMs, ms))
        gen.seekPreview(t)
        if (playing) media.position = t
    }
    function seekBy(ms) { seekTo(playheadMs + ms) }
    function stop() { media.stop(); gen.seekPreview(0) }
    Connections { target: gen; function onVideoChanged() { media.stop() } }

    FileDialog {
        id: videoDialog
        title: qsTr("Choose a video")
        nameFilters: [qsTr("Video (*.mp4 *.mkv *.webm *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: gen.openVideo(selectedFile.toString())
    }
    FileDialog {
        id: queueDialog
        title: qsTr("Add videos to the queue")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Video (*.mp4 *.mkv *.webm *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: gen.addToQueue(selectedFiles.map((u) => u.toString()))
    }
    FileDialog {
        id: scriptDialog
        title: qsTr("Open a script to edit")
        nameFilters: [qsTr("Scripts (*.funscript *.samn)"), qsTr("All files (*)")]
        onAccepted: edit.open(selectedFile.toString())
    }
    MessageDialog {
        id: overwriteDialog
        title: qsTr("Replace the script?")
        text: qsTr("A script already exists for this video. Creating again replaces these files.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: gen.create(true)
    }
    // A video from the Player (0.7 "Create script" / "always create"): open
    // it here; with `start`, find the tip and run as soon as Create can
    // (0.7 waited for the Generate button, at most 120 s).
    // The mark picked with a click on the frame (0.7: click a painted mark to
    // select it): -1 the tip box, 0… a contact area, -2 none. Delete removes it.
    property int selectedMark: -2
    function deleteSelectedMark() {
        if (gen.running) return
        gen.deleteMark(selectedMark)
        selectedMark = -2
    }
    // 0.7 "Selected: …" under the preview.
    function selectedMarkText() {
        if (selectedMark === -1) return qsTr("Selected: Tip (%1)").arg(className(gen.tipClass))
        if (selectedMark < 0 || selectedMark >= gen.contactAreas.length) return ""
        const cls = gen.contactAreas[selectedMark].cls
        if (selectedMark === 0) return qsTr("Selected: Contact (%1)").arg(className(cls))
        return cls ? qsTr("Selected: Extra contact #%1 (%2)").arg(selectedMark).arg(className(cls))
                   : qsTr("Selected: Extra contact #%1").arg(selectedMark)
    }
    function clearAllMarks() {
        gen.clearBox()
        gen.clearContactAreas()
        selectedMark = -2
    }
    Connections {
        target: gen
        function onContactChanged() { if (page.selectedMark >= gen.contactAreas.length) page.selectedMark = -2 }
        function onBoxChanged() { if (page.selectedMark === -1 && !gen.hasBox) page.selectedMark = -2 }
    }
    // From the Player: edit its script here, with its video (and the
    // Scenes tab when asked).
    function editScript(path, video, scenes) {
        if (video.toString() !== "" && video.toString() !== gen.videoUrl.toString() && !gen.running && !gen.queueRunning)
            gen.openVideo(video.toString())
        if (!edit.open(path)) return false
        page.sidePanel = scenes ? 1 : 0
        return true
    }
    // From the Player's "Create script": open the video and, when asked,
    // create at once (the controller finds the tip first, 0.7).
    function createFor(video, start) {
        if (gen.running || gen.queueRunning) return false
        if (!gen.openVideo(video.toString())) return false
        if (start) page.startCreate()
        return true
    }
    // 0.7 syncWorkflowSteps: one line on what to do next, verbatim.
    function stepPrompt() {
        if (!gen.hasVideo) return qsTr("Start here: choose a video. Like FunGen2 — no motion path to paint; we find the tip.")
        if (gen.running || (gen.finding && gen.status.startsWith(qsTr("Finding tip, then creating"))))
            return qsTr("Step 4: creating… you can Cancel if needed.")
        if (gen.outputPath !== "") return qsTr("Step 5: Improve, then Play — edit dots on the soft curve.")
        if (!gen.hasBox) return qsTr("Snack 1: Find tip (auto) — or mark only if Find missed. Tip = stroke box, not whole-scene tracking.")
        if (gen.contactVibration && gen.contactAreas.length === 0)
            return qsTr("Snack 2–3 optional: partner touch for Contact vibe, then Feel. Create unlocks when tip is ready.")
        return qsTr("Step 4: Create Emotion Script — Advanced stays closed for Everyday; open only if needed.")
    }
    // 0.7 roi_help.js coachText: the tip box and the first contact mark.
    function coachText() {
        const bits = []
        const r1 = gen.hasBox ? gen.box : null
        const a = gen.contactVibration && gen.contactAreas.length > 0 ? gen.contactAreas[0] : null
        if (r1) {
            if (r1.width * r1.height < 400) bits.push(qsTr("Tip box is very small — Find tip again or enlarge slightly."))
            if (r1.x < 4 || r1.y < 4) bits.push(qsTr("Tip box is flush with the image edge."))
            bits.push(qsTr("Tip box = CSRT stroke tracker."))
        } else {
            bits.push(qsTr("Press Find tip, or drag a tip box if Find missed."))
        }
        if (r1 && a) {
            const dx = (r1.x + r1.width / 2) - (a.x + a.w / 2)
            const dy = (r1.y + r1.height / 2) - (a.y + a.h / 2)
            if (Math.hypot(dx, dy) < 12) bits.push(qsTr("Contact mark almost on tip — move it to mouth/nipple."))
            bits.push(qsTr("Gold = optional Contact feel (not a second stroke writer)."))
        } else if (r1 && gen.contactVibration) {
            bits.push(qsTr("Optional: Mark contact for Contact vibe (mouth / nipple)."))
        }
        return bits.join(" ")
    }
    function startCreate() {
        if (gen.scriptExists) overwriteDialog.open()
        else gen.create(false)
    }
    function feelName(f) {
        const names = { "": qsTr("Classic"), oral: qsTr("Oral"), intercourse: qsTr("Intercourse"),
                        breast: qsTr("Breast"), hand: qsTr("Hand") }
        return names[f] !== undefined ? names[f] : f
    }
    function showScene(i) {
        const s = edit.scenes[i]
        if (!s) return
        page.selectedScene = i
        page.sidePanel = 1
        page.seekTo(s.startMs)
    }
    function fmt(ms) {
        return Math.floor(ms / 60000) + ":" + String(Math.floor(ms / 1000) % 60).padStart(2, "0")
    }
    // The stored class names (lower case, as 0.7 wrote them) for display.
    function className(c) {
        const names = { glans: qsTr("Glans"), penis: qsTr("Penis"), mouth: qsTr("Mouth"),
                        nipples: qsTr("Nipples"), breasts: qsTr("Breasts") }
        return names[c] || c
    }

    // The Player's keys (D-154): Space plays, arrows seek, , and . step.
    Shortcut { sequence: "Space"; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.togglePlay() }
    Shortcut { sequence: "Left"; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(-5000) }
    Shortcut { sequence: "Right"; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(5000) }
    Shortcut { sequence: "Shift+Left"; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(-1000) }
    Shortcut { sequence: "Shift+Right"; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(1000) }
    Shortcut { sequence: ","; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(-40) }
    Shortcut { sequence: "."; enabled: page.visible && !page.overlayOpen && gen.hasVideo; onActivated: page.seekBy(40) }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: page.visible && !page.overlayOpen && page.selectedMark !== -2; onActivated: page.deleteSelectedMark() }
    // Esc ends a marking, else drops the selection (0.7 "Mark deselected.").
    Shortcut {
        sequence: "Esc"
        enabled: page.visible && !page.overlayOpen && (page.selectedMark !== -2 || gen.markContact || gen.markExtra)
        onActivated: {
            if (gen.markContact || gen.markExtra) {
                gen.markContact = false
                gen.markExtra = false
                return
            }
            page.selectedMark = -2
            gen.showStatus(qsTr("Mark deselected."))
        }
    }
    Shortcut { sequences: [StandardKey.Undo]; enabled: page.visible && edit.canUndo; onActivated: edit.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: page.visible && edit.canRedo; onActivated: edit.redo() }

    RowLayout {
        anchors.fill: parent
        spacing: 16

        // ---- left column: project, source clips, scripts (D-154) ----
        GeneratorProjectPanel {
            id: projectPanel
            objectName: "projectPanel"
            Layout.fillHeight: true
            Layout.preferredWidth: width
            library: library
            generator: gen
            currentScript: edit.hasScript ? edit.path : ""
            onChooseVideo: videoDialog.open()
            onAddVideos: queueDialog.open()
            onOpenVideo: (p) => { if (!gen.running) gen.openVideo(p) }
            onOpenScript: (p) => edit.open(p)
        }

        // ---- main column: video, curves ----
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 12

            // Video with the tip box (2 Tip) and the frame seek row.
            GlassCard {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumHeight: 220
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Text {
                            textFormat: Text.PlainText
                            text: gen.hasVideo ? gen.videoName : qsTr("Create Emotion Script")
                            Layout.maximumWidth: 360; elide: Text.ElideMiddle
                            color: Theme.text; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold
                        }
                        Text {
                            textFormat: Text.PlainText
                            visible: gen.hasVideo
                            text: gen.videoInfo; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        Item { Layout.fillWidth: true }
                        // Step chips: done ✓, current highlighted.
                        Row {
                            objectName: "steps"
                            spacing: 6
                            Repeater {
                                model: [qsTr("Video"), qsTr("Tip"), qsTr("Feel"), qsTr("Create"), qsTr("Review")]
                                delegate: Rectangle {
                                    required property int index
                                    required property string modelData
                                    readonly property bool current: gen.step === index + 1
                                    readonly property bool done: gen.step > index + 1
                                    height: 24; width: chip.implicitWidth + 16; radius: 12
                                    color: current ? Qt.rgba(0, 217 / 255, 244 / 255, 0.22) : Qt.rgba(1, 1, 1, 0.04)
                                    border.color: current ? Theme.cyan : Theme.cardBorder
                                    Text {
                                        id: chip
                                        textFormat: Text.PlainText
                                        anchors.centerIn: parent
                                        text: (parent.done ? "✓ " : (parent.index + 1) + " ") + parent.modelData
                                        color: parent.current ? Theme.text : Theme.textDim
                                        font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "createLede"
                        visible: !gen.hasVideo
                        textFormat: Text.PlainText
                        text: qsTr("From a quiet video — motion becomes feel you can play.")
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Text {
                        objectName: "stepPrompt"
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        text: page.stepPrompt()
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Rectangle {
                        objectName: "generatorStage"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        radius: Theme.radius; color: "#02070f"; clip: true
                        SilkLayer { running: parent.visible && !gen.hasVideo && Qt.application.state === Qt.ApplicationActive }
                        VideoOutput {
                            id: videoOut
                            anchors.fill: parent
                            visible: page.playing
                        }
                        FrameItem {
                            id: frameView
                            objectName: "frameView"
                            anchors.fill: parent
                            visible: !page.playing
                            image: gen.frame
                        }
                        // The "0–100 on video" switch as a chip on the preview (0.7 toggle).
                        Rectangle {
                            objectName: "posOverlayToggle"
                            visible: gen.hasVideo && edit.hasScript
                            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: 8
                            z: 4
                            width: chipLabel.implicitWidth + 18; height: 22; radius: 11
                            color: gen.posOverlay ? Qt.rgba(0, 217 / 255, 244 / 255, 0.22) : Qt.rgba(2 / 255, 10 / 255, 20 / 255, 0.62)
                            border.color: gen.posOverlay ? Theme.cyan : Theme.cardBorder
                            Text {
                                id: chipLabel
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: qsTr("0–100 on video")
                                color: gen.posOverlay ? Theme.text : Theme.textDim
                                font.family: Theme.fontSoft; font.pixelSize: 11
                            }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: gen.posOverlay = !gen.posOverlay }
                        }
                        // 0.7 "0–100 on video": the script's stroke at the playhead.
                        Item {
                            id: gaugeHolder
                            objectName: "generatorGauge"
                            anchors.fill: parent
                            z: 3
                            visible: gen.hasVideo && gen.posOverlay && edit.hasScript
                            readonly property int value: edit.hasScript && edit.pointCount >= 0 ? edit.strokeAt(page.playheadMs) : -1  // pointCount: follows edits
                            PositionGauge {
                                visible: parent.height >= 150
                                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                                height: Math.min(190, parent.height - 24)
                                value: gaugeHolder.value
                            }
                            // A low preview: the value alone, never a squashed gauge.
                            Rectangle {
                                visible: parent.height < 150
                                anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                                width: 44; height: 26; radius: 13
                                color: Qt.rgba(2 / 255, 10 / 255, 20 / 255, 0.62); border.color: Theme.cardBorder
                                Text {
                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: gaugeHolder.value < 0 ? "—" : String(gaugeHolder.value)
                                    color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.bold: true
                                }
                            }
                        }
                        // Nothing open: the Player's round Play button opens a video.
                        Column {
                            objectName: "generatorEmpty"
                            anchors.centerIn: parent
                            visible: !gen.hasVideo
                            spacing: 14
                            PlayButton {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 96; height: 96
                                onClicked: videoDialog.open()
                            }
                            Text {
                                textFormat: Text.PlainText
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Choose video…")
                                color: Theme.text; font.family: Theme.font; font.pixelSize: 17
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: videoDialog.open() }
                            }
                            Text {
                                textFormat: Text.PlainText
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Then drag a box around the moving tip and create.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                        }
                        // Video pixels <-> item coordinates.
                        readonly property real scaleX: gen.videoSize.width > 0 ? frameView.imageRect.width / gen.videoSize.width : 1
                        readonly property real scaleY: gen.videoSize.height > 0 ? frameView.imageRect.height / gen.videoSize.height : 1
                        // The tip box: teal, as 0.7 (#3dccc0).
                        Rectangle {
                            objectName: "tipBox"
                            visible: gen.hasBox || (drag.active && !drag.contact)
                            readonly property rect r: drag.active && !drag.contact ? drag.rect : gen.box
                            x: frameView.imageRect.x + r.x * parent.scaleX
                            y: frameView.imageRect.y + r.y * parent.scaleY
                            width: r.width * parent.scaleX; height: r.height * parent.scaleY
                            color: Qt.rgba(61 / 255, 204 / 255, 192 / 255, page.selectedMark === -1 ? 0.30 : 0.16)
                            border.color: page.selectedMark === -1 ? "white" : "#3dccc0"; border.width: page.selectedMark === -1 ? 3 : 2
                        }
                        // Contact areas with their class and ×: the first gold, extras
                        // magenta (0.7 #f2b03d / #e070a0).
                        Repeater {
                            model: gen.contactVibration ? gen.contactAreas : []
                            delegate: Rectangle {
                                id: area
                                required property int index
                                required property var modelData
                                objectName: "contactArea_" + index
                                x: frameView.imageRect.x + modelData.x * parent.scaleX
                                y: frameView.imageRect.y + modelData.y * parent.scaleY
                                width: modelData.w * parent.scaleX; height: modelData.h * parent.scaleY
                                z: 2
                                readonly property color tone: index === 0 ? "#f2b03d" : "#e070a0"
                                readonly property bool picked: page.selectedMark === index
                                color: Qt.rgba(tone.r, tone.g, tone.b, picked ? 0.32 : 0.17)
                                border.color: picked ? "white" : tone; border.width: picked ? 3 : 2
                                Text {
                                    textFormat: Text.PlainText
                                    x: 4; y: 2
                                    text: page.className(area.modelData.cls)
                                    color: area.tone; font.family: Theme.fontSoft; font.pixelSize: 11
                                }
                                Text {
                                    objectName: "removeContact_" + area.index
                                    textFormat: Text.PlainText
                                    anchors.right: parent.right; anchors.rightMargin: 4; y: 1
                                    text: "×"; color: "white"; font.pixelSize: 14
                                    MouseArea {
                                        anchors.fill: parent; anchors.margins: -4
                                        cursorShape: Qt.PointingHandCursor
                                        enabled: !gen.running
                                        onClicked: gen.removeContactArea(area.index)
                                    }
                                }
                            }
                        }
                        Rectangle {
                            objectName: "contactDraft"
                            visible: drag.active && drag.contact
                            x: frameView.imageRect.x + drag.rect.x * parent.scaleX
                            y: frameView.imageRect.y + drag.rect.y * parent.scaleY
                            width: drag.rect.width * parent.scaleX; height: drag.rect.height * parent.scaleY
                            readonly property color tone: gen.markExtra && gen.contactAreas.length > 0 ? "#e070a0" : "#f2b03d"
                            color: Qt.rgba(tone.r, tone.g, tone.b, 0.17)
                            border.color: tone; border.width: 2
                        }
                        // Find tip's other proposals: dashed, a numbered chip picks one.
                        Repeater {
                            model: drag.active ? [] : gen.tipCandidates
                            delegate: Item {
                                id: cand
                                required property int index
                                required property var modelData
                                x: frameView.imageRect.x + modelData.x * parent.scaleX
                                y: frameView.imageRect.y + modelData.y * parent.scaleY
                                width: modelData.w * parent.scaleX; height: modelData.h * parent.scaleY
                                z: 2
                                Rectangle {
                                    anchors.fill: parent; color: "transparent"
                                    border.color: Qt.rgba(1, 1, 1, 0.55); border.width: 1
                                    opacity: 0.8
                                }
                                Rectangle {
                                    objectName: "candidate_" + cand.index
                                    width: 20; height: 20; radius: 10
                                    color: Qt.rgba(0, 0, 0, 0.6); border.color: "#3dccc0"
                                    Text {
                                        anchors.centerIn: parent; textFormat: Text.PlainText
                                        text: cand.index + 1; color: "white"; font.pixelSize: 11
                                    }
                                    MouseArea {
                                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        // 0.7: click = tip, Shift-click = the contact area.
                                        onClicked: (m) => {
                                            // keyboardModifiers: Windows QTest / some
                                            // drivers leave m.modifiers empty while Shift is held.
                                            const shift = (m.modifiers | Qt.keyboardModifiers) & Qt.ShiftModifier
                                            if (gen.contactVibration && shift) {
                                                const c = cand.modelData
                                                if (gen.addContactArea(Qt.rect(c.x, c.y, c.w, c.h))) page.selectedMark = 0
                                            } else {
                                                gen.pickCandidate(cand.index)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        MouseArea {
                            id: drag
                            objectName: "markArea"
                            anchors.fill: parent
                            enabled: gen.hasVideo && !gen.running && !page.playing
                            cursorShape: Qt.CrossCursor
                            property bool active: false
                            // This drag draws a contact area: Mark contact, + Another, or
                            // Shift held (0.7 Shift-drag = partner).
                            property bool contact: false
                            property point start
                            property rect rect
                            function toVideo(mx, my) {
                                return Qt.point((mx - frameView.imageRect.x) / parent.scaleX, (my - frameView.imageRect.y) / parent.scaleY)
                            }
                            onPressed: (m) => {
                                start = toVideo(m.x, m.y); rect = Qt.rect(start.x, start.y, 0, 0)
                                const shift = (m.modifiers | Qt.keyboardModifiers) & Qt.ShiftModifier
                                contact = gen.markContact || gen.markExtra || (gen.contactVibration && shift)
                                active = true
                            }
                            function spanTo(mx, my) {
                                const p = toVideo(mx, my)
                                rect = Qt.rect(Math.min(start.x, p.x), Math.min(start.y, p.y), Math.abs(p.x - start.x), Math.abs(p.y - start.y))
                            }
                            onPositionChanged: (m) => spanTo(m.x, m.y)
                            // The box ends where the button is let go.
                            onReleased: (m) => {
                                spanTo(m.x, m.y)
                                active = false
                                // A click (under 8 px on screen) picks the mark under it.
                                if (rect.width * parent.scaleX < 8 && rect.height * parent.scaleY < 8) {
                                    page.selectedMark = markAt(start)
                                    if (page.selectedMark !== -2)
                                        gen.showStatus(qsTr("Mark selected — set its class below, or Delete selected."))
                                    return
                                }
                                page.selectedMark = -2
                                const r = Qt.rect(Math.round(rect.x), Math.round(rect.y), Math.round(rect.width), Math.round(rect.height))
                                // 0.7 selects the mark just drawn.
                                if (contact) {
                                    const extra = gen.markExtra && gen.contactAreas.length > 0
                                    if (gen.addContactArea(r)) page.selectedMark = extra ? gen.contactAreas.length - 1 : 0
                                } else if (gen.markTip(r)) {
                                    page.selectedMark = -1
                                }
                            }
                            // Contact areas lie above the tip box; the last drawn wins.
                            function markAt(p) {
                                const areas = gen.contactVibration ? gen.contactAreas : []
                                for (let i = areas.length - 1; i >= 0; --i) {
                                    const a = areas[i]
                                    if (p.x >= a.x && p.x <= a.x + a.w && p.y >= a.y && p.y <= a.y + a.h) return i
                                }
                                const b = gen.box
                                if (gen.hasBox && p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height) return -1
                                return -2
                            }
                        }
                    }
                    Text {
                        objectName: "previewCoach"
                        visible: gen.hasVideo
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        text: page.coachText()
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    RowLayout {
                        visible: gen.hasVideo
                        spacing: 10
                        Text {
                            objectName: "boxLabel"
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; elide: Text.ElideRight
                            // A picked mark: 0.7's "Selected: …" with its class and Delete
                            // selected, in this row so the frame keeps its size.
                            text: page.selectedMark !== -2 ? page.selectedMarkText() : gen.boxLabel
                            color: page.selectedMark !== -2 ? Theme.text : Theme.textDim
                            font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        FlowCombo {
                            objectName: "selectedClassCombo"
                            visible: page.selectedMark !== -2
                            // The tip's class is the Tip combo's; an area has its own, an extra may be any.
                            readonly property bool isArea: page.selectedMark >= 0 && page.selectedMark < gen.contactAreas.length
                            readonly property var classes: isArea && page.selectedMark > 0 ? [""].concat(gen.contactClasses)
                                                                                            : isArea ? gen.contactClasses : gen.tipClasses
                            Layout.preferredWidth: 140
                            enabled: !gen.running
                            textRole: "label"; valueRole: "value"
                            model: classes.map((c) => ({ label: c ? page.className(c) : qsTr("(any)"), value: c }))
                            currentIndex: Math.max(0, classes.indexOf(isArea ? gen.contactAreas[page.selectedMark].cls : gen.tipClass))
                            onActivated: isArea ? gen.setAreaClass(page.selectedMark, currentValue) : gen.tipClass = currentValue
                        }
                        FlowButton {
                            objectName: "deleteSelectedMark"
                            visible: page.selectedMark !== -2
                            text: qsTr("Delete selected")
                            enabled: !gen.running
                            onClicked: page.deleteSelectedMark()
                        }
                        HelpChip { key: "gen-selected-mark-delete"; visible: page.selectedMark !== -2; Layout.alignment: Qt.AlignVCenter }
                        FlowButton {
                            objectName: "clearBox"
                            visible: page.selectedMark === -2 && (gen.hasBox || gen.contactAreas.length > 0)
                            text: qsTr("Clear all")
                            enabled: !gen.running
                            onClicked: page.clearAllMarks()
                        }
                        FlowButton {
                            objectName: "findTip"
                            text: gen.finding ? qsTr("Stop finding") : qsTr("Find tip"); icon: "search"
                            enabled: !gen.running
                            onClicked: gen.finding ? gen.cancelFind() : gen.findTip()
                        }
                        HelpChip { key: "gen-autoroi"; Layout.alignment: Qt.AlignVCenter }
                    }
                // The Player's transport (D-154), lower so the curves keep their room.
                VideoTransport {
                    id: transport
                    objectName: "generatorTransport"
                    compact: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: height
                    enabled: gen.hasVideo
                    opacity: enabled ? 1 : 0.5
                    position: page.playheadMs
                    duration: gen.durationMs
                    playing: page.playing
                    muted: page.muted
                    volume: 0.8
                    onSeekRequested: (ms) => page.seekTo(ms)
                    onSeekByRequested: (ms) => page.seekBy(ms)
                    onTogglePlayRequested: page.togglePlay()
                    onPreviousRequested: page.seekTo(0)
                    onStopRequested: page.stop()
                    onMuteToggled: page.muted = !page.muted
                    onVolumeMoved: (v) => transport.volume = v
                    leftItems: [
                        Text {
                            objectName: "generatorTime"
                            textFormat: Text.PlainText
                            y: 8
                            text: page.fmt(page.playheadMs) + " / " + page.fmt(gen.durationMs)
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        },
                        FlowCombo {
                            width: 78
                            model: ["0.5×", "0.75×", "1.0×", "1.25×", "1.5×", "2.0×"]
                            readonly property var rates: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                            currentIndex: rates.indexOf(page.speed)
                            onActivated: (i) => page.speed = rates[i]
                        }
                    ]
                }
                // 0.7 seek row: Time (s), +1 s, +5 s and the 0–100 gauge switch.
                // While marking (no script yet): after Create the editor's
                // timeline seeks, and the preview keeps its height.
                RowLayout {
                    objectName: "seekRow"
                    visible: gen.hasVideo && !edit.hasScript
                    Layout.fillWidth: true; spacing: 8
                    Text { textFormat: Text.PlainText; text: qsTr("Time (s)"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    HelpChip { key: "generator.time-s"; Layout.alignment: Qt.AlignVCenter }
                    FlowTextField {
                        id: timeField
                        objectName: "timeField"
                        Layout.preferredWidth: 80
                        maxLength: 10
                        placeholder: "0"
                        // Follows the playhead unless it is being typed in.
                        Binding on text {
                            when: !timeField.activeFocus
                            value: (page.playheadMs / 1000).toFixed(1)
                        }
                        onAccepted: {
                            const s = parseFloat(text.replace(",", "."))
                            if (!isNaN(s) && s >= 0) page.seekTo(Math.round(s * 2) / 2 * 1000)  // 0.7: step 0.5 s
                        }
                    }
                    FlowButton { objectName: "plus1s"; text: qsTr("+1s"); onClicked: page.seekBy(1000) }
                    FlowButton { objectName: "plus5s"; text: qsTr("+5s"); onClicked: page.seekBy(5000) }
                    Item { Layout.fillWidth: true }
                }
                }
            }

            // The curves: edit points, mark a range, range tools, undo.
            GlassCard {
                objectName: "editorCard"
                Layout.fillWidth: true
                Layout.preferredHeight: (edit.hasScript ? 344 : 96) + (gen.hasVideo ? 62 : 0) + (audioLane.visible ? 36 : 0)
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 6
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        Text {
                            textFormat: Text.PlainText
                            text: edit.hasScript ? edit.name : qsTr("Curves")
                            Layout.maximumWidth: 240; elide: Text.ElideMiddle
                            color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                        }
                        IconButton {
                            objectName: "undoButton"; icon: "undo"; tip: edit.undoText
                            enabled: edit.canUndo; opacity: enabled ? 1 : 0.4
                            onClicked: edit.undo()
                        }
                        IconButton {
                            objectName: "redoButton"; icon: "redo"; tip: edit.redoText
                            enabled: edit.canRedo; opacity: enabled ? 1 : 0.4
                            onClicked: edit.redo()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            textFormat: Text.PlainText
                            visible: edit.hasScript
                            text: edit.markerText
                            color: edit.hasMarker ? Theme.cyan : Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        IconButton {
                            objectName: "zoomMarker"; icon: "search"; tip: qsTr("Zoom to the marked range")
                            visible: edit.hasMarker; onClicked: edit.zoomToMarker()
                        }
                        IconButton {
                            objectName: "showAll"; icon: "timeline"; tip: qsTr("Show everything")
                            visible: edit.zoomed; onClicked: edit.showAll()
                        }
                        // Cock Hero / Fap Hero: one stroke per beat of the
                        // video's beat bar; works without a script too.
                        FlowButton {
                            objectName: "tool_beatbar"
                            visible: gen.hasVideo
                            enabled: !edit.busy && !gen.running
                            text: edit.busy && edit.progress > 0 ? qsTr("Beat bar %1 %").arg(edit.progress) : qsTr("Stroke from the beat bar")
                            icon: "waveform"
                            onClicked: edit.strokeFromBeatBar()
                        }
                        FlowButton {
                            objectName: "cancelTool"
                            visible: edit.busy
                            text: qsTr("Cancel")
                            onClicked: edit.cancelTool()
                        }
                        FlowButton {
                            objectName: "openScript"
                            text: qsTr("Open script…")
                            onClicked: scriptDialog.open()
                        }
                    }
                    // Range tools on the lane last touched (0.7 Play edit tools).
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        visible: edit.hasScript
                        FlowButton { objectName: "tool_delete"; text: qsTr("Delete"); icon: "trash"; enabled: edit.hasMarker; onClicked: edit.deleteRange() }
                        FlowButton { objectName: "tool_cap"; text: qsTr("Cap speed"); icon: "limits"; enabled: edit.hasMarker; onClicked: edit.capSpeed(0) }
                        FlowButton { objectName: "tool_softer"; text: qsTr("Softer"); icon: "minus"; enabled: edit.hasMarker; onClicked: edit.scaleRange(0.8, softEdges.checked) }
                        FlowButton { objectName: "tool_stronger"; text: qsTr("Stronger"); icon: "plus"; enabled: edit.hasMarker; onClicked: edit.scaleRange(1.25, softEdges.checked) }
                        FlowButton { objectName: "tool_simplify"; text: qsTr("Simplify"); icon: "curve"; enabled: edit.hasMarker; onClicked: edit.simplifyRange(2) }
                        FlowButton { objectName: "tool_invert"; text: edit.hasMarker ? qsTr("Invert range") : qsTr("Invert all"); icon: "invert"; onClicked: edit.invert() }
                        RowLayout {
                            spacing: 6
                            FlowToggle { id: softEdges; objectName: "softEdges"; checked: true }
                            Text { textFormat: Text.PlainText; text: qsTr("Soft edges"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                        }
                        // FPS snap (0.7 FPS-Snap): new and dragged points land on frame boundaries.
                        RowLayout {
                            spacing: 6
                            Text { textFormat: Text.PlainText; text: qsTr("Snap"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                            FlowSlider {
                                objectName: "fpsSnap"
                                from: 0; to: 60; Layout.preferredWidth: 110
                                value: edit.fpsSnap
                                onMoved: (v) => edit.fpsSnap = Math.round(v)
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: edit.fpsSnap > 0 ? qsTr("%1 fps").arg(Math.round(edit.fpsSnap)) : qsTr("off")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                    }
                    // Create tools: whole lanes from the stroke or the sound
                    // (0.7 bake and "Write as vibration curve"; one undo step each).
                    Flow {
                        Layout.fillWidth: true; spacing: 6
                        visible: edit.hasScript
                        enabled: !edit.busy && !gen.running
                        // 0.8 "Optimize for" (D-156): fill and heal the stroke for any
                        // device, or also write the Sam Neo 2 feel (contact vibration,
                        // vibration and suction).
                        FlowCombo {
                            id: optimizeTarget
                            objectName: "optimizeTarget"
                            width: 130; height: 36
                            readonly property var targets: ["general", "neo2"]
                            model: [qsTr("Any device"), qsTr("Sam Neo 2")]
                        }
                        FlowButton {
                            objectName: "tool_optimize"
                            text: qsTr("Optimize for"); icon: "sliders"
                            onClicked: edit.optimizeFor(optimizeTarget.targets[optimizeTarget.currentIndex])
                        }
                        FlowButton {
                            objectName: "tool_axes"
                            text: qsTr("Vibration & suction from the stroke"); icon: "effects"
                            onClicked: edit.axesFromStroke()
                        }
                        FlowButton {
                            objectName: "tool_sound"
                            text: edit.busy ? qsTr("Reading the sound…") : qsTr("Vibration from the sound"); icon: "audio"
                            enabled: gen.hasVideo
                            onClicked: edit.vibrationFromSound(keepContact.checked)
                        }
                        RowLayout {
                            spacing: 6
                            FlowToggle { id: keepContact; objectName: "keepContact"; checked: false }
                            Text { textFormat: Text.PlainText; text: qsTr("Keep contact vibration"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                        }
                    }
                    // Drag on the ruler marks a range; a click clears it.
                    RulerItem {
                        id: ruler
                        objectName: "ruler"
                        Layout.fillWidth: true; Layout.preferredHeight: 22
                        visible: edit.hasScript
                        viewStartMs: edit.viewStartMs; viewEndMs: edit.viewEndMs
                        playheadMs: page.playheadMs
                        textColor: Theme.textDim; markColor: Theme.laneVibration
                        MouseArea {
                            anchors.fill: parent
                            property real startMs: 0
                            function msAt(x) { return edit.viewStartMs + Math.max(0, Math.min(1, x / width)) * (edit.viewEndMs - edit.viewStartMs) }
                            onPressed: (m) => startMs = msAt(m.x)
                            onPositionChanged: (m) => edit.setMarker(startMs, msAt(m.x))
                            onReleased: (m) => {
                                const end = msAt(m.x)
                                if (Math.abs(end - startMs) < 40) { edit.clearMarker(); page.seekTo(end) }
                                else edit.setMarker(startMs, end)
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 2
                        visible: edit.hasScript
                        Repeater {
                            model: edit.lanes
                            delegate: CurveItem {
                                id: laneCurve
                                required property string modelData
                                objectName: "lane_" + modelData
                                Layout.fillWidth: true; Layout.fillHeight: true
                                Layout.preferredHeight: modelData === "stroke" ? 3 : 1
                                lane: modelData
                                label: modelData === "stroke" ? "" : (modelData === "vibration" ? qsTr("Vibration") : qsTr("Suction"))
                                lineColor: modelData === "stroke" ? Theme.orange : (modelData === "vibration" ? Theme.laneVibration : Theme.laneSuction)
                                showGrid: modelData === "stroke"
                                editMode: true
                                markerStartMs: edit.markerStartMs; markerEndMs: edit.markerEndMs
                                viewStartMs: edit.viewStartMs; viewEndMs: edit.viewEndMs
                                playheadMs: page.playheadMs
                                Component.onCompleted: edit.attachCurve(laneCurve)
                                Rectangle {
                                    anchors.fill: parent; color: "transparent"; radius: 4
                                    border.color: edit.activeLane === laneCurve.lane ? Qt.rgba(1, 1, 1, 0.18) : "transparent"
                                }
                                WheelHandler {
                                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                    onWheel: (e) => edit.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / laneCurve.width)
                                }
                                // Drag a point, click empty space to add one (and
                                // drag it), double-click to remove. The video follows.
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: edit.dragging ? Qt.ClosedHandCursor : Qt.CrossCursor
                                    onPressed: (m) => { const t = edit.press(laneCurve, m.x, m.y); if (t >= 0) page.seekTo(t) }
                                    onPositionChanged: (m) => { if (edit.dragging) { const t = edit.dragTo(m.x, m.y); if (t >= 0) page.seekTo(t) } }
                                    onReleased: edit.release()
                                    onDoubleClicked: (m) => edit.deleteAt(laneCurve, m.x, m.y)
                                }
                            }
                        }
                    }
                    // The video's sound on the same time scale, as the Player's
                    // audio lane (D-154).
                    AudioLevelItem {
                        id: audioLane
                        objectName: "generatorAudioLane"
                        Layout.fillWidth: true; Layout.preferredHeight: 34
                        visible: edit.hasScript && gen.hasVideo && !empty
                        source: gen.videoUrl
                        viewStartMs: edit.viewStartMs; viewEndMs: edit.viewEndMs
                        playheadMs: page.playheadMs
                        label: qsTr("Audio")
                        MouseArea {
                            anchors.fill: parent
                            onClicked: (m) => page.seekTo(edit.viewStartMs + m.x / width * (edit.viewEndMs - edit.viewStartMs))
                        }
                    }
                    // The scenes (the script's chapters) as bands on the shown
                    // time (D-145); a click selects one in the Scenes panel.
                    Item {
                        id: sceneStrip
                        objectName: "sceneStrip"
                        Layout.fillWidth: true; Layout.preferredHeight: 20
                        visible: edit.hasScript && edit.scenes.length > 0
                        clip: true
                        Repeater {
                            model: sceneStrip.visible ? edit.scenes : []
                            delegate: Rectangle {
                                id: band
                                required property int index
                                required property var modelData
                                objectName: "sceneBand_" + index
                                readonly property real span: Math.max(1, edit.viewEndMs - edit.viewStartMs)
                                x: (modelData.startMs - edit.viewStartMs) / span * sceneStrip.width
                                width: Math.max(2, (modelData.endMs - modelData.startMs) / span * sceneStrip.width - 1)
                                height: sceneStrip.height; radius: 4
                                readonly property color tone: page.sceneColors[index % 5]
                                color: Qt.rgba(tone.r, tone.g, tone.b, page.selectedScene === index ? 0.45 : 0.22)
                                border.color: page.selectedScene === index ? tone : "transparent"
                                Text {
                                    textFormat: Text.PlainText
                                    anchors.fill: parent; anchors.leftMargin: 6
                                    verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                    text: band.modelData.name + (band.modelData.hasFeel ? " · " + page.feelName(band.modelData.feel) : "")
                                    color: "white"; font.family: Theme.fontSoft; font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: page.showScene(band.index)
                                }
                            }
                        }
                    }
                    // Single frames along the shown time (D-144); a click
                    // shows that moment. Times snap to a power-of-two grid, so
                    // zooming meets the same frames again in the cache.
                    Item {
                        id: strip
                        objectName: "frameStrip"
                        Layout.fillWidth: true; Layout.preferredHeight: 54
                        visible: gen.hasVideo
                        readonly property real t0: edit.hasScript ? edit.viewStartMs : 0
                        readonly property real t1: edit.hasScript ? edit.viewEndMs : gen.durationMs
                        readonly property int count: Math.max(1, Math.floor(width / 86))
                        readonly property real grid: {
                            const step = Math.max(1, (t1 - t0) / count)
                            return Math.pow(2, Math.floor(Math.log2(step)))
                        }
                        Row {
                            anchors.fill: parent; spacing: 2
                            Repeater {
                                model: strip.visible ? strip.count : 0
                                delegate: Item {
                                    id: thumb
                                    required property int index
                                    readonly property real ms: {
                                        const t = strip.t0 + (index + 0.5) * (strip.t1 - strip.t0) / strip.count
                                        return Math.min(Math.max(0, gen.durationMs - 1), Math.round(t / strip.grid) * strip.grid)
                                    }
                                    readonly property bool current: page.playheadMs >= strip.t0 + index * (strip.t1 - strip.t0) / strip.count
                                                                   && page.playheadMs < strip.t0 + (index + 1) * (strip.t1 - strip.t0) / strip.count
                                    width: strip.width / strip.count - 2; height: strip.height
                                    Image {
                                        objectName: "frameThumb"
                                        anchors.fill: parent
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectCrop
                                        sourceSize.height: 108
                                        source: "image://frames/" + Math.round(thumb.ms) + "/108/" + encodeURIComponent(gen.videoPath)
                                    }
                                    Rectangle {
                                        anchors.fill: parent; radius: 3; color: "transparent"
                                        border.width: thumb.current ? 2 : 1
                                        border.color: thumb.current ? Theme.cyan : Qt.rgba(1, 1, 1, 0.08)
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: page.seekTo(thumb.ms)
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "editorStatus"
                        textFormat: Text.PlainText
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: edit.status !== "" ? edit.status : (edit.hasScript ? "" : qsTr("Create a script, open one, or take the stroke from a beat bar."))
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                }
            }
        }

        // ---- right panel: video, feel, create | scenes ----
        ColumnLayout {
            Layout.preferredWidth: 340; Layout.maximumWidth: 340
            Layout.fillHeight: true
            spacing: 12

            FlowTabs {
                objectName: "sideTabs"
                model: [qsTr("Create"), qsTr("Scenes") + (edit.scenes.length > 0 ? " (" + edit.scenes.length + ")" : "")]
                current: page.sidePanel
                onPicked: (i) => page.sidePanel = i
            }

            // Scenes (D-145, 0.7 Scene Map): one card per scene with its
            // frame, range, feel and suggestion.
            GlassCard {
                objectName: "scenesCard"
                visible: page.sidePanel === 1
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: edit.scenes.length === 1 ? qsTr("1 scene") : qsTr("%1 scenes").arg(edit.scenes.length)
                            color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                        }
                        FlowButton {
                            objectName: "addScene"
                            text: qsTr("+ Scene here")
                            enabled: edit.hasScript
                            onClicked: if (edit.addScene(page.playheadMs)) page.selectedScene = -1
                        }
                    }
                    FlowButton {
                        objectName: "scenesFromPhases"
                        visible: edit.hasPhases
                        text: qsTr("Scenes from phases")
                        onClicked: edit.scenesFromPhases()
                    }
                    // D-157: the phases shape what the device gets when the
                    // .samn plays; the curve itself stays as it is.
                    FlowButton {
                        objectName: "phasesToPlayback"
                        visible: edit.hasPhases
                        text: edit.playbackPhases > 0 ? qsTr("Update phase playback") : qsTr("Phases shape playback")
                        onClicked: edit.phasesToPlayback()
                    }
                    FlowButton {
                        objectName: "clearPlaybackPhases"
                        visible: edit.playbackPhases > 0
                        text: qsTr("Play without phases (%1)").arg(edit.playbackPhases)
                        onClicked: edit.clearPlaybackPhases()
                    }
                    FlowButton {
                        objectName: "applySuggestions"
                        visible: edit.hasSceneSuggestions
                        text: qsTr("Apply suggestions")
                        onClicked: edit.applySceneSuggestions()
                    }
                    Text {
                        textFormat: Text.PlainText
                        visible: edit.scenes.length === 0
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: edit.hasScript
                              ? qsTr("No scenes yet. Scenes are the script's chapters: show a frame and press \"+ Scene here\".")
                              : qsTr("Open or create a script to mark its scenes.")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    ListView {
                        id: sceneList
                        objectName: "sceneList"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 8
                        model: edit.scenes
                        delegate: Rectangle {
                            id: card
                            required property int index
                            required property var modelData
                            objectName: "sceneCard_" + index
                            width: sceneList.width
                            height: cardCol.implicitHeight + 16; radius: 12
                            readonly property color tone: page.sceneColors[index % 5]
                            color: page.selectedScene === index ? Qt.rgba(0, 120 / 255, 1, 0.14) : Qt.rgba(1, 1, 1, 0.03)
                            border.color: page.selectedScene === index ? tone : Theme.cardBorder
                            MouseArea { anchors.fill: parent; onClicked: page.showScene(card.index) }
                            ColumnLayout {
                                id: cardCol
                                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                                anchors.margins: 8; spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Image {
                                        visible: gen.hasVideo
                                        Layout.preferredWidth: 96; Layout.preferredHeight: 54
                                        asynchronous: true; fillMode: Image.PreserveAspectCrop
                                        sourceSize.height: 108
                                        // The frame just inside the scene's start (0.7 thumbFor).
                                        source: gen.hasVideo ? "image://frames/" + Math.round(card.modelData.startMs + 40) + "/108/" + encodeURIComponent(gen.videoPath) : ""
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 2
                                        FlowTextField {
                                            objectName: "sceneName_" + card.index
                                            Layout.fillWidth: true; implicitHeight: 28
                                            text: card.modelData.name
                                            onAccepted: edit.renameScene(card.index, text)
                                        }
                                        Text {
                                            textFormat: Text.PlainText
                                            text: page.fmt(card.modelData.startMs) + " – " + page.fmt(card.modelData.endMs)
                                                  + " · " + page.fmt(card.modelData.endMs - card.modelData.startMs)
                                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                        }
                                    }
                                    IconButton {
                                        objectName: "deleteScene_" + card.index
                                        icon: "trash"; tip: qsTr("Remove this scene")
                                        onClicked: { edit.deleteScene(card.index); page.selectedScene = -1 }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Text {
                                        textFormat: Text.PlainText
                                        text: qsTr("Feel"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                    FlowCombo {
                                        objectName: "sceneFeel_" + card.index
                                        Layout.fillWidth: true
                                        textRole: "label"; valueRole: "value"
                                        // "-": the script's own feel; "" is Classic.
                                        model: [{ label: qsTr("Script default"), value: "-" }].concat(
                                                   edit.sceneFeels.map((f) => ({ label: page.feelName(f), value: f })))
                                        // Computed from the list, not indexOfValue: that runs before the model is set.
                                        currentIndex: card.modelData.hasFeel ? 1 + Math.max(0, edit.sceneFeels.indexOf(card.modelData.feel)) : 0
                                        onActivated: currentValue === "-" ? edit.clearSceneFeel(card.index) : edit.setSceneFeel(card.index, currentValue)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Text {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                                        text: card.modelData.suggested !== ""
                                              ? qsTr("Suggested: %1 · %2 %3%").arg(page.feelName(card.modelData.suggested))
                                                    .arg(card.modelData.source).arg(Math.round(card.modelData.confidence * 100))
                                              : qsTr("No suggestion for this part.")
                                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                                    }
                                    FlowButton {
                                        objectName: "useSuggestion_" + card.index
                                        visible: card.modelData.suggested !== "" && !(card.modelData.hasFeel && card.modelData.feel === card.modelData.suggested)
                                        text: qsTr("Use")
                                        onClicked: edit.setSceneFeel(card.index, card.modelData.suggested)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 1 Video
            GlassCard {
                visible: page.sidePanel === 0
                Layout.fillWidth: true; implicitHeight: videoCol.implicitHeight + 28
                ColumnLayout {
                    id: videoCol
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("1 · Video"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    Text {
                        objectName: "videoLabel"
                        textFormat: Text.PlainText
                        Layout.fillWidth: true; elide: Text.ElideMiddle
                        text: gen.hasVideo ? gen.videoName : qsTr("No video selected")
                        color: gen.hasVideo ? Theme.text : Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        FlowButton {
                            objectName: "chooseVideo"
                            text: qsTr("Choose video…"); primary: !gen.hasVideo
                            enabled: !gen.running
                            onClicked: videoDialog.open()
                        }
                        FlowButton {
                            objectName: "usePlayerVideo"
                            visible: page.playerVideoUrl.toString() !== ""
                            enabled: !gen.running
                            text: qsTr("Use Player video")
                            onClicked: gen.openVideo(page.playerVideoUrl.toString())
                        }
                    }
                }
            }

            // 3 Feel
            GlassCard {
                visible: page.sidePanel === 0
                Layout.fillWidth: true; implicitHeight: feelCol.implicitHeight + 28
                opacity: gen.hasVideo ? 1 : 0.5
                ColumnLayout {
                    id: feelCol
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("3 · How it feels"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        FlowCombo {
                            objectName: "styleCombo"
                            Layout.fillWidth: true
                            enabled: !gen.running
                            textRole: "label"; valueRole: "value"
                            model: [{ label: qsTr("Normal"), value: "standard" },
                                    { label: qsTr("Soft"), value: "weich" },
                                    { label: qsTr("Autotune"), value: "autotune" }]
                            currentIndex: Math.max(0, indexOfValue(gen.profile))
                            onActivated: gen.profile = currentValue
                        }
                        HelpChip { key: "generator.style"; Layout.alignment: Qt.AlignVCenter }
                    }
                    Repeater {
                        model: [{ text: qsTr("Invert motion direction"), key: "invert", help: "gen-invert" },
                                { text: qsTr("Camera motion compensation"), key: "cameraCompensation", help: "gen-camcomp" },
                                { text: qsTr("Scene-cut detection"), key: "sceneCuts", help: "gen-scenecut" },
                                { text: qsTr("Suggest O-zone automatically"), key: "autoOZone", help: "gen-auto-ozone" }]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: modelData.text; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                            HelpChip { key: modelData.help; Layout.alignment: Qt.AlignVCenter }
                            FlowToggle {
                                objectName: "toggle_" + modelData.key
                                enabled: !gen.running
                                checked: gen[modelData.key]
                                onToggled: (c) => gen[modelData.key] = c
                            }
                        }
                    }
                    // Contact vibration (0.7 Step 2 "Tip & contact"): the
                    // classes and areas show only while it is on.
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: qsTr("Contact vibration"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                        HelpChip { key: "gen-contact-vibration"; Layout.alignment: Qt.AlignVCenter }
                        FlowToggle {
                            objectName: "toggle_contactVibration"
                            enabled: !gen.running
                            checked: gen.contactVibration
                            onToggled: (c) => gen.contactVibration = c
                        }
                    }
                    ColumnLayout {
                        objectName: "contactMarks"
                        visible: gen.contactVibration
                        Layout.fillWidth: true; spacing: 6
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Tip"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowCombo {
                                objectName: "tipClassCombo"
                                Layout.fillWidth: true
                                enabled: !gen.running
                                textRole: "label"; valueRole: "value"
                                model: gen.tipClasses.map((c) => ({ label: page.className(c), value: c }))
                                currentIndex: Math.max(0, indexOfValue(gen.tipClass))
                                onActivated: gen.tipClass = currentValue
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Contact"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowCombo {
                                objectName: "contactClassCombo"
                                Layout.fillWidth: true
                                enabled: !gen.running
                                textRole: "label"; valueRole: "value"
                                model: gen.contactClasses.map((c) => ({ label: page.className(c), value: c }))
                                currentIndex: Math.max(0, indexOfValue(gen.contactClass))
                                onActivated: gen.contactClass = currentValue
                            }
                        }
                        // 0.7 Step 3: how deep the stroke must go and how the vibration rises.
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Sensitivity"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            HelpChip { key: "generator.sensitivity"; Layout.alignment: Qt.AlignVCenter }
                            FlowSlider {
                                objectName: "contactSpan"
                                Layout.fillWidth: true
                                enabled: !gen.running
                                from: 0.40; to: 0.95; value: gen.contactSpan
                                onMoved: (v) => gen.contactSpan = v
                            }
                            Text {
                                objectName: "contactSpanLabel"
                                textFormat: Text.PlainText
                                // 0.7: the contact starts at this share of the stroke depth, so a high
                                // value means deep strokes only (<= 50 "earlier", >= 85 "deep only").
                                text: gen.contactSpan <= 0.5 ? qsTr("earlier") : gen.contactSpan >= 0.85 ? qsTr("deep only") : gen.contactSpan.toFixed(2)
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Curve"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            HelpChip { key: "generator.curve"; Layout.alignment: Qt.AlignVCenter }
                            FlowCombo {
                                objectName: "contactCurveCombo"
                                Layout.fillWidth: true
                                enabled: !gen.running
                                textRole: "label"; valueRole: "value"
                                model: [{ label: qsTr("Linear"), value: "linear" },
                                        { label: qsTr("Soft onset (contact-like)"), value: "soft" },
                                        { label: qsTr("Stronger peak"), value: "peak" },
                                        { label: qsTr("Impulse (peaks only, experiment)"), value: "impulse" }]
                                currentIndex: Math.max(0, ["linear", "soft", "peak", "impulse"].indexOf(gen.contactCurve))
                                onActivated: gen.contactCurve = currentValue
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowButton {
                                objectName: "markContact"
                                text: gen.markContact ? qsTr("Drag the area on the frame…") : qsTr("Mark contact area")
                                enabled: gen.hasVideo && !gen.running
                                onClicked: gen.markContact = !gen.markContact
                            }
                            HelpChip { key: "gen-roi2-toggle"; Layout.alignment: Qt.AlignVCenter }
                            FlowButton {
                                objectName: "clearContact"
                                visible: gen.contactAreas.length > 0
                                enabled: !gen.running
                                text: qsTr("Clear areas")
                                onClicked: gen.clearContactAreas()
                            }
                        }
                        // G2b / 0.7: Partner fixed (pixels) — gold box stays put.
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: qsTr("Partner fixed (pixels)")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            HelpChip { key: "gen-roi2-fixed"; Layout.alignment: Qt.AlignVCenter }
                            FlowToggle {
                                objectName: "partnerFixed"
                                checked: gen.partnerFixed
                                onToggled: (c) => gen.partnerFixed = c
                            }
                        }
                        // 0.7 "+ Another contact area" (magenta) with its type.
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowButton {
                                objectName: "addExtra"
                                text: gen.markExtra ? qsTr("Drag the area on the frame…") : qsTr("+ Another contact area")
                                enabled: gen.hasVideo && !gen.running
                                onClicked: gen.markExtra = !gen.markExtra
                            }
                            HelpChip { key: "gen-target-add"; Layout.alignment: Qt.AlignVCenter }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Extra type"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowCombo {
                                objectName: "extraClassCombo"
                                Layout.fillWidth: true
                                enabled: !gen.running
                                textRole: "label"; valueRole: "value"
                                model: [{ label: qsTr("(any)"), value: "" }].concat(gen.contactClasses.map((c) => ({ label: page.className(c), value: c })))
                                currentIndex: Math.max(0, indexOfValue(gen.extraClass))
                                onActivated: gen.extraClass = currentValue
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: gen.contactAreas.length > 0
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: qsTr("Stay fixed (extras)")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            HelpChip { key: "gen-extra-contact-sticky"; Layout.alignment: Qt.AlignVCenter }
                            FlowToggle {
                                objectName: "extrasStayFixed"
                                checked: gen.extrasStayFixed
                                onToggled: (c) => gen.extrasStayFixed = c
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: gen.contactAreas.length > 1
                            FlowButton {
                                objectName: "clearExtras"
                                enabled: !gen.running
                                text: qsTr("Clear extras")
                                onClicked: gen.clearExtras()
                            }
                            HelpChip { key: "gen-extras-clear"; Layout.alignment: Qt.AlignVCenter }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: gen.extrasStayFixed
                                      ? qsTr("Extras stay where they are drawn; only the tip is tracked today.")
                                      : qsTr("Extras marked to follow the partner path (tracking engine next).")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: gen.contactAreas.length === 1
                                  ? qsTr("1 contact area: the toy vibrates when the tip reaches it.")
                                  : gen.contactAreas.length > 1
                                  ? qsTr("%1 contact areas: the toy vibrates when the tip reaches one.").arg(gen.contactAreas.length)
                                  : qsTr("Optional: mark where the tip makes contact; without areas the vibration follows the stroke's depth.")
                            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
            }

            // 4 Create
            GlassCard {
                visible: page.sidePanel === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                // Scrolls when the run's log, the quality box and the queue need more room.
                Flickable {
                    id: createFlick
                    anchors.fill: parent; anchors.margins: 14
                    clip: true
                    contentHeight: createCol.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    // A finished run brings its quality box into view.
                    Connections {
                        target: gen
                        function onCreated() {
                            Qt.callLater(() => {
                                const bottom = qualityBox.y + qualityBox.height
                                if (bottom > createFlick.contentY + createFlick.height)
                                    createFlick.contentY = Math.min(createFlick.contentHeight - createFlick.height, bottom - createFlick.height)
                            })
                        }
                    }
                ColumnLayout {
                    id: createCol
                    width: createFlick.width; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("4 · Create"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    RowLayout {
                        spacing: 8
                        FlowButton {
                            objectName: "createButton"
                            text: qsTr("Create Emotion Script"); primary: true
                            // No tip yet: Create finds it first (0.7).
                            enabled: gen.hasVideo && !gen.running && !gen.finding
                            onClicked: page.startCreate()
                        }
                        FlowButton {
                            objectName: "cancelButton"
                            text: qsTr("Cancel"); visible: gen.running || gen.queueRunning || gen.finding
                            onClicked: gen.cancel()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Text {
                            textFormat: Text.PlainText
                            text: qsTr("Optimize after Create")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        FlowCombo {
                            objectName: "optimizeAfterCreate"
                            Layout.fillWidth: true
                            readonly property var targets: ["", "general", "neo2"]
                            model: [qsTr("Off — keep as created"), qsTr("Any device"), qsTr("Sam Neo 2")]
                            currentIndex: Math.max(0, targets.indexOf(gen.optimizeTarget))
                            onActivated: (i) => gen.optimizeTarget = targets[i]
                        }
                        HelpChip { key: "gen-optimize-target"; Layout.alignment: Qt.AlignVCenter }
                    }
                    Rectangle {
                        objectName: "progressBar"
                        Layout.fillWidth: true; height: 6; radius: 3
                        visible: gen.running || gen.queueRunning || gen.percent > 0
                        color: Qt.rgba(1, 1, 1, 0.08)
                        Rectangle {
                            height: parent.height; radius: 3
                            // Unknown length: a full, faded bar (0.7).
                            width: gen.percent < 0 ? parent.width : parent.width * gen.percent / 100
                            opacity: gen.percent < 0 ? 0.35 : 1
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.blue }
                                GradientStop { position: 1; color: Theme.cyan }
                            }
                            Behavior on width { NumberAnimation { duration: Theme.animFast } }
                        }
                    }
                    Text {
                        objectName: "progressText"
                        textFormat: Text.PlainText
                        visible: gen.progressText !== ""
                        text: gen.progressText; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Text {
                        objectName: "status"
                        textFormat: Text.PlainText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: gen.status; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    // 0.7's quality box and post-generate review after a run.
                    Rectangle {
                        id: qualityBox
                        objectName: "qualityBox"
                        readonly property var r: gen.review
                        readonly property bool ok: r.qualityPassed === true
                        visible: r.quality !== undefined && !gen.running
                        Layout.fillWidth: true
                        implicitHeight: qualityCol.implicitHeight + 16
                        radius: 8
                        color: ok ? Qt.rgba(61 / 255, 216 / 255, 117 / 255, 0.12) : Qt.rgba(216 / 255, 77 / 255, 77 / 255, 0.12)
                        border.color: ok ? Theme.good : Theme.danger
                        ColumnLayout {
                            id: qualityCol
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                            spacing: 4
                            Text {
                                objectName: "qualityLine"
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: qsTr("Signal Quality (Quality Doctor): %1% %2").arg(qualityBox.r.quality)
                                      .arg(qualityBox.ok ? qsTr("(within normal)") : qsTr("(review recommended)"))
                                color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13; font.bold: true
                            }
                            Text {
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: qsTr("Technical signal quality — not motion fidelity (Does the curve match the video?).")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Repeater {
                                model: qualityBox.r.qualityWarnings || []
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                    text: "•  " + modelData
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                            }
                            Text {
                                objectName: "trackingLine"
                                visible: text !== ""
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: {
                                    const why = qualityBox.r.trackingReason || ""
                                    if (why === "tracker_lost_heavy") return qsTr("Tracking: heavy tracker loss — Tip/Partner often lost; contact feel may mute in gaps")
                                    if (why === "tracker_lost_elevated") return qsTr("Tracking: elevated tracker loss — some Tip/Partner gaps")
                                    return why !== "" ? qsTr("Tracking: %1").arg(why) : ""
                                }
                                color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            // postgen.js: direction and O-zone, a hint only.
                            Text {
                                objectName: "reviewLine"
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: qsTr("Direction: %1").arg(qualityBox.r.direction || "") + "\n" + (qualityBox.r.ozone || "")
                                      + (qualityBox.r.suggestInvert ? qsTr(" (Direction uncertain — review in Play; invert curve if needed.)") : "")
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                    }
                    FlowButton {
                        objectName: "reviewButton"
                        visible: gen.outputPath !== "" || edit.hasScript
                        text: qsTr("Play in the Player")
                        icon: "play"
                        onClicked: page.reviewRequested(edit.hasScript ? edit.playPath : gen.outputPath)
                    }
                    // The queue (0.7 "Create queue"): each video gets its tip
                    // found and its script created with the settings above.
                    RowLayout {
                        Layout.fillWidth: true; spacing: 6
                        FlowButton {
                            objectName: "addToQueue"
                            text: qsTr("Add to queue…"); icon: "playlist"
                            enabled: !gen.queueRunning
                            onClicked: queueDialog.open()
                        }
                        FlowButton {
                            objectName: "runQueue"
                            visible: gen.queue.length > 0
                            text: qsTr("Create queue")
                            enabled: !gen.queueRunning && !gen.running && gen.queue.some((q) => q.status === "queued")
                            onClicked: gen.runQueue(false)
                        }
                        HelpChip { key: "gen-queue-run"; visible: gen.queue.length > 0; Layout.alignment: Qt.AlignVCenter }
                        FlowButton {
                            objectName: "clearQueue"
                            visible: gen.queue.length > 0 && !gen.queueRunning
                            text: qsTr("Clear")
                            onClicked: gen.clearQueue()
                        }
                        HelpChip { key: "gen-queue-clear"; visible: gen.queue.length > 0 && !gen.queueRunning; Layout.alignment: Qt.AlignVCenter }
                    }
                    Text {
                        objectName: "queueSummary"
                        textFormat: Text.PlainText
                        visible: gen.queueSummary !== ""
                        text: gen.queueSummary; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Column {
                        objectName: "queueList"
                        Layout.fillWidth: true; spacing: 2
                        Repeater {
                            model: gen.queue
                            delegate: RowLayout {
                                required property int index
                                required property var modelData
                                width: parent.width; spacing: 6
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.preferredWidth: 18
                                    text: modelData.status === "done" ? "✓" : modelData.status === "running" ? "…"
                                        : modelData.status === "failed" ? "✗" : modelData.status === "queued" ? String(index + 1) : "–"
                                    color: modelData.status === "done" ? Theme.cyan : modelData.status === "failed" ? Theme.orange : Theme.textDim
                                    font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; elide: Text.ElideMiddle
                                    text: modelData.name + (modelData.message !== "" && modelData.status !== "done" ? " · " + modelData.message : "")
                                    color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                                // One clip out of the queue (not while it runs).
                                IconButton {
                                    objectName: "removeQueued_" + index
                                    width: 22; height: 22
                                    icon: "close"; tip: qsTr("Remove from the queue")
                                    visible: !gen.queueRunning
                                    onClicked: gen.removeFromQueue(index)
                                }
                            }
                        }
                    }
                    ListView {
                        objectName: "runLog"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 2
                        model: gen.log
                        delegate: Text {
                            required property string modelData
                            textFormat: Text.PlainText
                            width: ListView.view.width; wrapMode: Text.WordWrap
                            text: modelData; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
                }
            }
        }
    }
}
