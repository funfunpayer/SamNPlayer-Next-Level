import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtMultimedia
import SamN

// Training mode (0.7 Training tab, the Owner's target docs/visual-target/training.png):
// programs on the left; the program, the glowing rings, the session bar, the live
// curves and the timeline in the middle; settings, mode, safety, feedback and the
// sessions on the right. All logic lives in TrainingController (C++).
Item {
    id: page
    property alias controller: ctl
    required property DeviceController device
    // The mode deck uses Esc while it is open.
    property bool overlayOpen: false
    // Test hook (SAMN_TRAINING_START in main.cpp): start this program ("custom" = the form) at once.
    property string autostart: ""

    TrainingController {
        id: ctl
        device: page.device
    }

    FileDialog {
        id: stageVideoDialog
        title: qsTr("Choose a video to play along")
        nameFilters: [qsTr("Video (*.mp4 *.mkv *.webm *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: ctl.chooseVideo(selectedFile.toString())
    }

    // Phase tint from the planned strength: calm cyan, orange, red (0.7 heatColor).
    function heat(level) {
        const stops = [[54, 200, 255], [255, 138, 35], [255, 59, 92]]
        const v = Math.max(0, Math.min(1, (level - 0.25) / 0.55)) * (stops.length - 1)
        const i = Math.min(stops.length - 2, Math.floor(v)), f = v - i
        const c = stops[i].map((a, k) => Math.round(a + (stops[i + 1][k] - a) * f))
        return Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, 1)
    }
    function clock(sec) {
        const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60
        const two = n => (n < 10 ? "0" : "") + n
        return (h > 0 ? h + ":" : "") + two(m) + ":" + two(s)
    }
    readonly property bool custom: ctl.scriptName === ""
    readonly property int total: ctl.planTotalMs
    readonly property real progress: total > 0 ? Math.min(1, ctl.elapsedSec * 1000 / total) : 0
    readonly property var heroTexts: ({ idle: qsTr("Ready"), connecting: qsTr("Connecting..."), running: qsTr("Running"), paused: qsTr("Paused") })

    Shortcut {
        sequence: "Esc"
        enabled: page.visible && !page.overlayOpen && !editor.open && ctl.state === "running"
        onActivated: ctl.interrupt()
    }
    Component.onCompleted: {
        if (autostart !== "") {
            if (autostart !== "custom") ctl.scriptName = autostart
            ctl.start()
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // ------------------------------------------------------------ left: programs
        GlassCard {
            Layout.preferredWidth: 232; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14
                spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Training"); color: Theme.text; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: qsTr("Category"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11 }
                Flow {
                    Layout.fillWidth: true; spacing: 4
                    Repeater {
                        model: [["", qsTr("All")], ["control", qsTr("Control")], ["endurance", qsTr("Endurance")], ["edge", qsTr("Edge Play")], ["body", qsTr("Body")]]
                        delegate: Rectangle {
                            required property var modelData
                            readonly property bool on: ctl.category === modelData[0]
                            height: 24; width: chip.implicitWidth + 16; radius: 12
                            color: on ? Qt.rgba(0, 120 / 255, 1, 0.30) : chipArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                            border.color: on ? Theme.cyan : Theme.cardBorder
                            Text { textFormat: Text.PlainText; id: chip; anchors.centerIn: parent; text: parent.modelData[1]; color: parent.on ? Theme.text : Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            MouseArea { id: chipArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ctl.category = parent.modelData[0] }
                        }
                    }
                }
                Text { textFormat: Text.PlainText; text: qsTr("Programs"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11; Layout.topMargin: 4 }
                ListView {
                    id: programList
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 2
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }
                    model: [{ name: "", label: qsTr("Custom"), text: qsTr("Custom (Stop-Start / Plateau)"), level: 0, categoryLabel: "" }].concat(ctl.programs)
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: ctl.scriptName === modelData.name
                        width: ListView.view.width - 8; height: 38; radius: 10
                        color: on ? Qt.rgba(0, 120 / 255, 1, 0.32) : rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                        border.color: on ? Theme.cyan : "transparent"
                        enabled: ctl.state === "idle"
                        opacity: enabled ? 1 : 0.5
                        Column {
                            anchors.verticalCenter: parent.verticalCenter; x: 10; width: parent.width - 20; spacing: 1
                            Text { textFormat: Text.PlainText; width: parent.width; elide: Text.ElideRight; text: modelData.label; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
                            Text {
                                textFormat: Text.PlainText
                                visible: modelData.level > 0
                                text: modelData.categoryLabel + "  L" + modelData.level
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 10; font.letterSpacing: 0.4
                            }
                        }
                        MouseArea { id: rowArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ctl.scriptName = modelData.name }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { textFormat: Text.PlainText; text: qsTr("My programs"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11; Layout.fillWidth: true }
                    IconButton { icon: "plus"; width: 28; height: 28; tip: qsTr("New program"); onClicked: editor.show("") }
                }
                Text {
                    textFormat: Text.PlainText
                    visible: ctl.myPrograms.length === 0
                    text: qsTr("None saved yet"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                Repeater {
                    model: ctl.myPrograms
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: ctl.scriptName === modelData.name
                        Layout.fillWidth: true; Layout.preferredHeight: 32; radius: 10
                        color: on ? Qt.rgba(0, 120 / 255, 1, 0.32) : mineArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                        border.color: on ? Theme.cyan : "transparent"
                        enabled: ctl.state === "idle"
                        opacity: enabled ? 1 : 0.5
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; x: 10; width: parent.width - 20; elide: Text.ElideRight; text: modelData.label; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        MouseArea { id: mineArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: ctl.scriptName = modelData.name }
                    }
                }
            }
        }

        // ------------------------------------------------------------ centre
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 10

            // Program and phase stepper.
            GlassCard {
                Layout.fillWidth: true; Layout.preferredHeight: 104
                RowLayout {
                    anchors.fill: parent; anchors.margins: 14
                    spacing: 18
                    ColumnLayout {
                        Layout.preferredWidth: 300; Layout.fillHeight: true
                        spacing: 6
                        Text { textFormat: Text.PlainText; text: qsTr("Training Program"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14; font.weight: Font.Medium }
                        RowLayout {
                            spacing: 8
                            FlowCombo {
                                id: programBox
                                Layout.fillWidth: true
                                enabled: ctl.state === "idle"
                                textRole: "text"
                                model: [{ name: "", text: qsTr("Custom (Stop-Start / Plateau + timings)") }].concat(ctl.programs, ctl.myPrograms)
                                currentIndex: Math.max(0, model.findIndex(p => p.name === ctl.scriptName))
                                onActivated: (i) => ctl.scriptName = model[i].name
                            }
                            IconButton { icon: "edit"; width: 34; height: 34; visible: ctl.customScript; enabled: ctl.state === "idle"; tip: qsTr("Edit this program"); onClicked: editor.show(ctl.scriptName) }
                            IconButton { icon: "plus"; width: 34; height: 34; enabled: ctl.state === "idle"; tip: qsTr("New program"); onClicked: editor.show("") }
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; Layout.fillHeight: true
                            text: page.custom ? qsTr("Stop-Start or Plateau with the timings on the right.") : ctl.scriptDescription
                            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                            wrapMode: Text.WordWrap; elide: Text.ElideRight; maximumLineCount: 2
                        }
                    }
                    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; Layout.topMargin: 4; Layout.bottomMargin: 4; color: Theme.cardBorder }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Training Phase"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14; font.weight: Font.Medium }
                        Item {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            readonly property int count: ctl.planPhases.length
                            readonly property real slot: count > 0 ? width / count : width
                            // The line behind the steps.
                            Rectangle { x: parent.slot / 2; y: 11; width: Math.max(0, parent.width - parent.slot); height: 2; color: Qt.rgba(1, 1, 1, 0.14) }
                            Repeater {
                                model: ctl.planPhases
                                delegate: Item {
                                    required property int index
                                    required property var modelData
                                    readonly property bool current: ctl.active ? ctl.phaseIndex === index : index === 0
                                    readonly property bool done: ctl.active && ctl.phaseIndex > index
                                    x: index * parent.slot; width: parent.slot; height: parent.height
                                    Rectangle {
                                        id: dot
                                        x: parent.width / 2 - width / 2; y: 0; width: 24; height: 24; radius: 12
                                        color: parent.current ? Theme.blue : parent.done ? Qt.rgba(0, 217 / 255, 244 / 255, 0.35) : "#0a1a2e"
                                        border.color: parent.current || parent.done ? Theme.cyan : Qt.rgba(1, 1, 1, 0.3); border.width: 1.5
                                        Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: parent.parent.index + 1; color: "#ffffff"; font.family: Theme.fontSoft; font.pixelSize: 11 }
                                    }
                                    Text {
                                        textFormat: Text.PlainText
                                        visible: ctl.planPhases.length <= 4
                                        anchors.top: dot.bottom; anchors.topMargin: 4; width: parent.width
                                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                                        text: parent.modelData.name
                                        color: parent.current ? Theme.text : Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The rings stage.
            GlassCard {
                id: stage
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 200
                clip: true
                // 0.8 #550 rings stage: calm dark navy with the silk "infinity
                // bridge" (stage-ribbons) drifting behind the two gauges; the
                // window's own silk bands stay outside (Owner, 19 Oct).
                Shape {
                    anchors.fill: parent
                    ShapePath {
                        strokeColor: "transparent"
                        fillGradient: RadialGradient {
                            centerX: stage.width / 2; centerY: stage.height * 0.48
                            focalX: centerX; focalY: centerY
                            centerRadius: Math.max(stage.width, stage.height) * 0.62
                            GradientStop { position: 0; color: Qt.rgba(10 / 255, 42 / 255, 72 / 255, 0.55) }
                            GradientStop { position: 1; color: Qt.rgba(2 / 255, 8 / 255, 18 / 255, 0.95) }
                        }
                        // A rounded rectangle (PathRectangle needs Qt 6.8).
                        startX: Theme.radius; startY: 0
                        PathLine { x: stage.width - Theme.radius; y: 0 }
                        PathArc { x: stage.width; y: Theme.radius; radiusX: Theme.radius; radiusY: Theme.radius }
                        PathLine { x: stage.width; y: stage.height - Theme.radius }
                        PathArc { x: stage.width - Theme.radius; y: stage.height; radiusX: Theme.radius; radiusY: Theme.radius }
                        PathLine { x: Theme.radius; y: stage.height }
                        PathArc { x: 0; y: stage.height - Theme.radius; radiusX: Theme.radius; radiusY: Theme.radius }
                        PathLine { x: 0; y: Theme.radius }
                        PathArc { x: Theme.radius; y: 0; radiusX: Theme.radius; radiusY: Theme.radius }
                    }
                }
                readonly property bool videoStage: ctl.stage === "video"
                Image {
                    id: bridge
                    objectName: "silkBridge"
                    visible: ctl.stage === "rings"
                    source: "assets/stage-ribbons.svg"
                    sourceSize.width: 2000
                    fillMode: Image.Stretch
                    width: stage.width * 1.2; height: stage.height * 1.0
                    x: (stage.width - width) / 2 + dx; y: (stage.height - height) / 2 + stage.height * 0.04
                    opacity: 0.92
                    asynchronous: true
                    property real dx: 0
                    // silk-bridge-drift: 28 s alternate, ±1.5 %, scale 1.04; pause when hidden.
                    readonly property bool drift: stage.visible && ctl.stage === "rings"
                        && Qt.application.state === Qt.ApplicationActive
                    SequentialAnimation on dx {
                        running: bridge.drift; loops: Animation.Infinite
                        NumberAnimation { from: -stage.width * 0.015; to: stage.width * 0.015; duration: 28000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: -stage.width * 0.015; duration: 28000; easing.type: Easing.InOutSine }
                    }
                    SequentialAnimation on scale {
                        running: bridge.drift; loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 1.04; duration: 28000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 1.0; duration: 28000; easing.type: Easing.InOutSine }
                    }
                    SequentialAnimation on opacity {
                        running: bridge.drift; loops: Animation.Infinite
                        NumberAnimation { from: 0.78; to: 0.96; duration: 9000; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0.78; duration: 9000; easing.type: Easing.InOutSine }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    x: 16; y: 12
                    text: qsTr("LIVE TRAINING") + (ctl.active ? "  ·  " + ctl.phaseName : "")
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11; font.letterSpacing: 2
                }
                // Video stage (0.8 training_video.js): the user's film plays
                // along while the training runs and pauses when it stops; the
                // device follows the training, not the film.
                MediaPlayer {
                    id: stageMedia
                    objectName: "stageMedia"
                    source: stage.videoStage ? ctl.videoUrl : ""
                    videoOutput: stageVideo
                    loops: ctl.videoLoop ? MediaPlayer.Infinite : 1
                    audioOutput: AudioOutput { muted: ctl.videoMuted }
                    readonly property bool shouldPlay: stage.videoStage && ctl.state === "running"
                    onShouldPlayChanged: shouldPlay ? play() : pause()
                    onMediaStatusChanged: if (mediaStatus === MediaPlayer.LoadedMedia && shouldPlay) play()
                    // D-157: past intro and empty stretches; a climax acts on the session.
                    onPositionChanged: {
                        if (!stage.videoStage) return
                        const to = ctl.videoPosition(position)
                        if (to >= 0) setPosition(to)
                    }
                }
                VideoOutput {
                    id: stageVideo
                    visible: stage.videoStage && ctl.videoPath !== ""
                    anchors.fill: parent; anchors.margins: 1
                    anchors.topMargin: 52
                }
                Text {
                    objectName: "stageVideoHint"
                    textFormat: Text.PlainText
                    visible: stage.videoStage && ctl.videoPath === ""
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 60, 420)
                    horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: qsTr("Choose a video — it plays along while the training runs. The device follows the training, not the film.")
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
                }
                Row {
                    objectName: "stageVideoControls"
                    visible: stage.videoStage
                    anchors.left: parent.left; anchors.leftMargin: 16
                    anchors.top: parent.top; anchors.topMargin: 36
                    spacing: 12
                    FlowButton {
                        objectName: "stageVideoChoose"
                        height: 30
                        text: qsTr("Choose video…")
                        onClicked: stageVideoDialog.open()
                    }
                    FlowCombo {
                        objectName: "stageClimaxAction"
                        visible: ctl.videoPhases.length > 0
                        width: 190; height: 30
                        readonly property var actions: ["none", "pause", "cooldown"]
                        model: [qsTr("Climax: carry on"), qsTr("Climax: pause"), qsTr("Climax: cool-down")]
                        currentIndex: actions.indexOf(ctl.climaxAction)
                        onActivated: (i) => ctl.climaxAction = actions[i]
                    }
                    FlowCombo {
                        objectName: "stageVideoRecent"
                        width: 200; height: 30
                        enabled: ctl.recentVideos.length > 0
                        model: ctl.recentVideos.map((p) => p.split(/[\\/]/).pop())
                        displayText: ctl.videoName !== "" ? ctl.videoName : qsTr("Recent videos")
                        onActivated: (i) => ctl.chooseVideo(ctl.recentVideos[i])
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter; spacing: 6
                        FlowToggle { objectName: "stageVideoLoop"; anchors.verticalCenter: parent.verticalCenter; checked: ctl.videoLoop; onToggled: (on) => ctl.videoLoop = on }
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Loop"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                    Row {
                        anchors.verticalCenter: parent.verticalCenter; spacing: 6
                        FlowToggle { objectName: "stageVideoMuted"; anchors.verticalCenter: parent.verticalCenter; checked: ctl.videoMuted; onToggled: (on) => ctl.videoMuted = on }
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Muted"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                }
                // The Flow figure (D-152, in place of 0.7's pixel clip): it follows
                // the live level; while no session runs it shows the technique's
                // shape (0.7 pixel_figure.js: stop-start 1600 ms ramp 45 % · hold
                // 10 % · down 45 %, plateau 2200 ms 0.72 + 0.18 sin, times the peak).
                Item {
                    id: figureStage
                    objectName: "figureStage"
                    visible: ctl.stage === "figure"
                    anchors.fill: parent; anchors.topMargin: 44; anchors.bottomMargin: 56
                    readonly property bool live: ctl.state === "running"
                    readonly property real liveLevel: Math.max(ctl.vibrationAvailable ? ctl.vibration : 0, ctl.suctionAvailable ? ctl.suction : 0)
                    property real demo: 0
                    property real previous: 0
                    property bool rising: false
                    readonly property real level: live ? liveLevel : demo
                    onLevelChanged: { if (Math.abs(level - previous) > 0.002) rising = level > previous; previous = level }
                    Timer {
                        interval: 40; repeat: true
                        running: figureStage.visible && !figureStage.live
                        property real t0: Date.now()
                        onTriggered: {
                            const t = Date.now() - t0, peak = ctl.peak
                            if (ctl.technique === "plateau") {
                                figureStage.demo = (0.72 + 0.18 * Math.sin(t / 2200 * 2 * Math.PI)) * peak
                            } else {
                                const f = (t % 1600) / 1600
                                figureStage.demo = (f < 0.45 ? f / 0.45 : f < 0.55 ? 1 : (1 - f) / 0.45) * peak
                            }
                        }
                    }
                    FlowFigure {
                        anchors.fill: parent
                        level: figureStage.level
                        tint: page.heat(0.25 + figureStage.level * 0.55)
                    }
                }
                // 0.7's dual ring beside the clip: both levels in one ring.
                DualRing {
                    objectName: "dualRing"
                    visible: figureStage.visible
                    anchors.left: parent.left; anchors.leftMargin: 18
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 14
                    vibration: ctl.vibrationAvailable ? ctl.vibration : 0
                    suction: ctl.suctionAvailable ? ctl.suction : 0
                    running: ctl.state === "running"
                }
                Column {
                    visible: figureStage.visible
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 14
                    spacing: 2
                    Text {
                        objectName: "figureReadout"
                        textFormat: Text.PlainText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (ctl.technique === "plateau" ? qsTr("Plateau (edge)") : qsTr("Stop-start")) + "  ·  " +
                              (figureStage.live ? Math.round(figureStage.level * 100) + "% · " + (figureStage.rising ? qsTr("up") : qsTr("down"))
                                                : qsTr("Idle"))
                        color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: ctl.lastFeedback > 0 ? qsTr("Last feedback %1").arg(ctl.lastFeedback) : qsTr("No feedback yet")
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                }
                // The stage switch (0.8 Clip · Rings · Waves · Video; the waves
                // have their own card here, D-152).
                FlowTabs {
                    objectName: "stageSwitch"
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.top: parent.top; anchors.topMargin: 6
                    readonly property var modes: ["rings", "figure", "video"]
                    model: [qsTr("Rings"), qsTr("Figure"), qsTr("Video")]
                    current: Math.max(0, modes.indexOf(ctl.stage))
                    onPicked: (i) => ctl.stage = modes[i]
                }
                Row {
                    id: rings
                    visible: opacity > 0.01
                    opacity: ctl.stage === "rings" ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic } }
                    anchors.centerIn: parent
                    spacing: Math.max(30, stage.width * 0.12)
                    scale: ctl.stage === "rings" ? 1 : 0.96
                    Behavior on scale { NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic } }
                    TrainingRing {
                        visible: ctl.vibrationAvailable
                        width: Math.min(236, stage.height - 64); height: width
                        label: qsTr("Vibration"); level: ctl.vibration; tint: Theme.trainVibration; live: ctl.state === "running"
                    }
                    TrainingRing {
                        visible: ctl.suctionAvailable
                        width: Math.min(236, stage.height - 64); height: width
                        label: qsTr("Suction"); level: ctl.suction; tint: Theme.trainSuction; live: ctl.state === "running"
                    }
                }
                // Why Start does nothing, or what the device lacks (flashes when Start is refused).
                Rectangle {
                    id: noteBox
                    visible: ctl.deviceNote !== ""
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: 10
                    width: Math.min(parent.width - 40, noteText.implicitWidth + 28); height: noteText.implicitHeight + 14; radius: 10
                    color: "#cc0a1a2e"; border.color: Theme.orange
                    Text { textFormat: Text.PlainText; id: noteText; anchors.centerIn: parent; width: parent.width - 24; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: ctl.deviceNote; color: Theme.orange; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    SequentialAnimation {
                        id: flash
                        NumberAnimation { target: noteBox; property: "scale"; to: 1.06; duration: 120 }
                        NumberAnimation { target: noteBox; property: "scale"; to: 1.0; duration: 160 }
                    }
                }
                Connections { target: ctl; function onStartBlocked() { flash.restart() } }
            }

            // Session bar: time, transport, phase.
            GlassCard {
                Layout.fillWidth: true; Layout.preferredHeight: 92
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.topMargin: 10; anchors.bottomMargin: 10
                    spacing: 16
                    RowLayout {
                        Layout.preferredWidth: 250; spacing: 12
                        Icon { name: "clock"; width: 26; height: 26; color: Theme.textDim }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Text { textFormat: Text.PlainText; text: qsTr("Session Time"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            Text {
                                textFormat: Text.PlainText
                                text: page.clock(ctl.elapsedSec) + " / " + page.clock(Math.round(page.total / 1000))
                                color: Theme.text; font.family: Theme.font; font.pixelSize: 18; font.weight: Font.DemiBold
                            }
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 5; radius: 2.5; color: Qt.rgba(1, 1, 1, 0.10)
                                Rectangle { width: parent.width * page.progress; height: parent.height; radius: 2.5; color: Theme.cyan }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignHCenter
                        spacing: 16
                        Item { Layout.fillWidth: true }
                        IconButton {
                            icon: "stop"; width: 46; height: 46; enabled: ctl.active
                            tip: qsTr("End session"); onClicked: ctl.stop()
                        }
                        PlayButton {
                            Layout.preferredWidth: 72; Layout.preferredHeight: 72
                            playing: ctl.state === "running"
                            onClicked: ctl.state === "idle" ? ctl.start() : ctl.state === "connecting" ? ctl.stop() : ctl.togglePause()
                        }
                        IconButton {
                            icon: "skipForward"; width: 46; height: 46; enabled: ctl.state === "running"
                            tip: qsTr("Interrupt now (Esc)"); onClicked: ctl.interrupt()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.preferredWidth: 250; spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 2
                            Text { textFormat: Text.PlainText; text: qsTr("Phase"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            Text { textFormat: Text.PlainText; Layout.fillWidth: true; elide: Text.ElideRight; text: ctl.phaseName; color: Theme.text; font.family: Theme.font; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: ctl.state === "paused" ? qsTr("Paused · step %1 of %2").arg(ctl.phaseIndex + 1).arg(ctl.phaseCount)
                                    : ctl.state === "connecting" ? qsTr("Connecting...")
                                    : qsTr("Step %1 of %2").arg(ctl.phaseIndex + 1).arg(ctl.phaseCount)
                                color: Theme.cyanSoft; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                        ColumnLayout {
                            Layout.preferredWidth: 78; spacing: 3
                            Text { textFormat: Text.PlainText; text: qsTr("Time Left"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            Text { textFormat: Text.PlainText; text: page.clock(ctl.remainingSec); color: Theme.text; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold }
                            Rectangle {
                                Layout.fillWidth: true; Layout.preferredHeight: 5; radius: 2.5; color: Qt.rgba(1, 1, 1, 0.10)
                                Rectangle { width: parent.width * (1 - page.progress); height: parent.height; radius: 2.5; color: Theme.orange }
                            }
                        }
                    }
                }
            }

            // Live curves.
            GlassCard {
                Layout.fillWidth: true; Layout.preferredHeight: 150
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true; spacing: 14
                        Text { textFormat: Text.PlainText; text: qsTr("Live Training Curves"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14; font.weight: Font.Medium; Layout.fillWidth: true }
                        Row {
                            spacing: 14
                            Row { spacing: 6; Rectangle { y: 3; width: 9; height: 9; radius: 5; color: Theme.trainVibration } Text { textFormat: Text.PlainText; text: qsTr("Vibration"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 } }
                            Row { spacing: 6; Rectangle { y: 3; width: 9; height: 9; radius: 5; color: Theme.trainSuction } Text { textFormat: Text.PlainText; text: qsTr("Suction"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 } }
                            Text { textFormat: Text.PlainText; text: "– – " + qsTr("Target"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        }
                    }
                    TrainingCurveItem {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        controller: ctl; live: true
                    }
                }
            }

            // Timeline.
            GlassCard {
                Layout.fillWidth: true; Layout.preferredHeight: 116
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Text { textFormat: Text.PlainText; text: qsTr("Session Timeline"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14; font.weight: Font.Medium; Layout.fillWidth: true }
                        Text { textFormat: Text.PlainText; text: qsTr("Total  %1").arg(page.clock(Math.round(page.total / 1000))); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                    }
                    // One band, tinted by the planned strength of each phase, with the head on it.
                    Item {
                        id: band
                        Layout.fillWidth: true; Layout.preferredHeight: 8
                        Row {
                            Repeater {
                                model: ctl.planPhases
                                delegate: Rectangle {
                                    required property var modelData
                                    width: page.total > 0 ? band.width * (modelData.endMs - modelData.startMs) / page.total : 0
                                    height: 4; y: 2
                                    color: page.heat(modelData.strength)
                                }
                            }
                        }
                        Rectangle {
                            x: band.width * page.progress - 5; y: -1; width: 10; height: 10; radius: 5
                            color: "#ffffff"; border.color: Theme.orange; border.width: 2
                            visible: ctl.active
                        }
                    }
                    Row {
                        id: tiles
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 8
                        // Tiles for programs; a Custom plan of many cycles keeps only the band.
                        visible: ctl.planPhases.length <= 6
                        Repeater {
                            model: ctl.planPhases
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                readonly property bool current: ctl.active && ctl.phaseIndex === index
                                width: (tiles.width - tiles.spacing * (ctl.planPhases.length - 1)) / Math.max(1, ctl.planPhases.length)
                                height: tiles.height; radius: 10; clip: true
                                color: Qt.rgba(1, 1, 1, 0.03)
                                border.color: current ? Theme.cyan : Theme.cardBorder; border.width: current ? 1.5 : 1
                                TrainingCurveItem {
                                    anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4; anchors.topMargin: 2; anchors.bottomMargin: 28; opacity: 0.85
                                    controller: ctl; viewStartMs: parent.modelData.startMs; viewEndMs: parent.modelData.endMs
                                }
                                Row {
                                    x: 8; anchors.bottom: parent.bottom; anchors.bottomMargin: 6; spacing: 8
                                    Rectangle { width: 20; height: 20; radius: 10; color: page.heat(parent.parent.modelData.strength)
                                        Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: parent.parent.parent.index + 1; color: "#06121f"; font.family: Theme.fontSoft; font.pixelSize: 11; font.weight: Font.DemiBold } }
                                    Column {
                                        Text { textFormat: Text.PlainText; text: parent.parent.parent.modelData.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 11; font.weight: Font.Medium }
                                        Text { textFormat: Text.PlainText; text: page.clock(Math.round(parent.parent.parent.modelData.startMs / 1000)) + " - " + page.clock(Math.round(parent.parent.parent.modelData.endMs / 1000)); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 10 }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------ right
        Flickable {
            Layout.preferredWidth: 380; Layout.fillHeight: true
            contentWidth: width; contentHeight: side.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            ColumnLayout {
                id: side
                width: parent.width - 10
                spacing: 10

                // Session settings.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: settings.implicitHeight + 28
                    ColumnLayout {
                        id: settings
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: qsTr("Session Settings"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium; Layout.fillWidth: true }
                            Text { textFormat: Text.PlainText; text: qsTr("Mock (test without device)"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            FlowToggle { checked: ctl.mock; enabled: ctl.state === "idle"; onToggled: (on) => ctl.mock = on }
                        }
                        // A program: the intensity tier and the history nudge.
                        ColumnLayout {
                            visible: !page.custom
                            Layout.fillWidth: true; spacing: 8
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Text { textFormat: Text.PlainText; text: qsTr("Intensity"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13; Layout.preferredWidth: 90 }
                                Repeater {
                                    model: [[0.8, qsTr("Gentle")], [1, qsTr("Standard")], [1.2, qsTr("Intense")]]
                                    delegate: FlowButton {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: modelData[1]; checked: ctl.tier === modelData[0]; enabled: ctl.state === "idle"
                                        onClicked: ctl.tier = modelData[0]
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Auto-adjust from recent sessions with this profile"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
                                FlowToggle { checked: ctl.autoAdjust; enabled: ctl.state === "idle"; onToggled: (on) => ctl.autoAdjust = on }
                            }
                            Text { textFormat: Text.PlainText; visible: ctl.intensityNote !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.intensityNote; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            Text { textFormat: Text.PlainText; visible: ctl.historySuggestion !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.historySuggestion; color: Theme.orange; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            Text { textFormat: Text.PlainText; visible: ctl.planHasJitter; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "⚡ " + qsTr("Randomized — the curve is the nominal plan; actual peaks vary a bit each cycle."); color: Theme.cyanSoft; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        }
                        // Custom: the manual fields.
                        ColumnLayout {
                            visible: page.custom
                            enabled: ctl.state === "idle"
                            Layout.fillWidth: true; spacing: 6
                            SettingRow { Layout.fillWidth: true; label: qsTr("Cycles"); help: "training.cycles"; from: 1; to: 20; step: 1; value: ctl.cycles; display: String(ctl.cycles); tip: qsTr("Number of training cycles (1-20)."); onMoved: (v) => ctl.cycles = v }
                            SettingRow { Layout.fillWidth: true; label: qsTr("Ramp up (ms)"); help: "training.ramp-up-ms"; from: 0; to: 30000; step: 500; value: ctl.rampUpMs; display: String(ctl.rampUpMs); tip: qsTr("Ramp-up duration per cycle."); onMoved: (v) => ctl.rampUpMs = v }
                            SettingRow { Layout.fillWidth: true; label: qsTr("Hold (ms)"); help: "training.hold-ms"; from: 0; to: 15000; step: 500; value: ctl.holdMs; display: String(ctl.holdMs); tip: qsTr("Hold duration at peak."); onMoved: (v) => ctl.holdMs = v }
                            SettingRow { Layout.fillWidth: true; label: qsTr("Rest (ms)"); help: "training.rest-ms"; from: 0; to: 60000; step: 500; value: ctl.restMs; display: String(ctl.restMs); tip: qsTr("Rest between cycles."); onMoved: (v) => ctl.restMs = v }
                            SettingRow { Layout.fillWidth: true; label: qsTr("Peak intensity"); help: "training.peak-intensity"; from: 0; to: 1; step: 0.05; value: ctl.peak; display: ctl.peak.toFixed(2); tip: qsTr("Peak level for each cycle (0-1)."); onMoved: (v) => ctl.peak = v }
                            SettingRow { Layout.fillWidth: true; visible: ctl.technique === "plateau"; label: qsTr("Plateau fraction"); help: "training.plateau-fraction"; from: 0; to: 1; step: 0.05; value: ctl.plateauFraction; display: ctl.plateauFraction.toFixed(2); tip: qsTr("How high the plateau sits relative to the peak (edging)."); onMoved: (v) => ctl.plateauFraction = v }
                            SettingRow { Layout.fillWidth: true; label: qsTr("Progression"); help: "training.progression-per-cycle"; from: 0; to: 1; step: 0.05; value: ctl.progression; display: ctl.progression.toFixed(2); tip: qsTr("How much the peak grows each cycle (0-1)."); onMoved: (v) => ctl.progression = v }
                        }
                    }
                }

                // Session mode (Custom only).
                GlassCard {
                    visible: page.custom
                    Layout.fillWidth: true; Layout.preferredHeight: mode.implicitHeight + 28
                    ColumnLayout {
                        id: mode
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Session Mode"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Repeater {
                                model: [["stopstart", qsTr("Stop-Start"), qsTr("Back to 0 between cycles"), "waveform"], ["plateau", qsTr("Plateau"), qsTr("Stays high between peaks (edging)"), "curve"]]
                                delegate: Rectangle {
                                    required property var modelData
                                    readonly property bool on: ctl.technique === modelData[0]
                                    Layout.fillWidth: true; Layout.preferredHeight: 74; radius: 12
                                    color: on ? Qt.rgba(0, 120 / 255, 1, 0.30) : modeArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.03)
                                    border.color: on ? Theme.cyan : Theme.cardBorder
                                    opacity: ctl.state === "idle" ? 1 : 0.5
                                    Column {
                                        anchors.centerIn: parent; spacing: 3; width: parent.width - 16
                                        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: parent.parent.modelData[3]; width: 22; height: 22; color: parent.parent.on ? Theme.cyanSoft : Theme.textDim }
                                        Text { textFormat: Text.PlainText; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: parent.parent.modelData[1]; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
                                        Text { textFormat: Text.PlainText; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: parent.parent.modelData[2]; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 10 }
                                    }
                                    MouseArea { id: modeArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; enabled: ctl.state === "idle"; onClicked: ctl.technique = parent.modelData[0] }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Channel"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13; Layout.preferredWidth: 90 }
                            FlowCombo {
                                Layout.fillWidth: true
                                enabled: ctl.state === "idle" && ctl.channelOptions.length > 1
                                textRole: "text"
                                model: ctl.channelOptions.map(c => ({ value: c, text: c === "vibration" ? qsTr("Vibration only") : c === "suction" ? qsTr("Suction only") : qsTr("Both") }))
                                currentIndex: Math.max(0, ctl.channelOptions.indexOf(ctl.channel))
                                onActivated: (i) => ctl.channel = model[i].value
                            }
                        }
                    }
                }

                // Safety.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: safety.implicitHeight + 28
                    ColumnLayout {
                        id: safety
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Icon { name: "shield"; width: 20; height: 20; color: Theme.cyanSoft }
                            Text { textFormat: Text.PlainText; text: qsTr("Safety Controls"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 10
                            Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Esc interrupts the current cycle at any time."); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                            FlowButton { text: qsTr("Stop session"); icon: "warning"; danger: true; enabled: ctl.active; onClicked: ctl.emergencyStop() }
                        }
                    }
                }

                // Feedback.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: feedback.implicitHeight + 28
                    ColumnLayout {
                        id: feedback
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Feedback"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium }
                        Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("How close are you right now? High values → shorter, gentler next cycles with longer rest. Target is 7."); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        Row {
                            spacing: 5
                            Repeater {
                                model: 10
                                delegate: Rectangle {
                                    required property int index
                                    readonly property int level: index + 1
                                    width: 28; height: 28; radius: 14
                                    color: ctl.lastFeedback === level ? Qt.rgba(0, 120 / 255, 1, 0.45) : fbArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03)
                                    border.color: ctl.lastFeedback === level ? Theme.cyan : level === 7 ? Theme.orange : Theme.cardBorder
                                    border.width: level === 7 ? 2 : 1
                                    opacity: ctl.active ? 1 : 0.45
                                    Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: parent.level; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
                                    MouseArea { id: fbArea; anchors.fill: parent; hoverEnabled: true; enabled: ctl.active; cursorShape: Qt.PointingHandCursor; onClicked: ctl.reportFeedback(parent.level) }
                                    ToolTipText { text: parent.level === 7 ? qsTr("Feedback 7 (target)") : qsTr("Feedback %1").arg(parent.level); shown: fbArea.containsMouse }
                                }
                            }
                        }
                        Text { textFormat: Text.PlainText; visible: ctl.feedbackStatus !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.feedbackStatus; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        Text { textFormat: Text.PlainText; visible: ctl.feedbackEffect !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.feedbackEffect; color: Theme.cyanSoft; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        // The live readouts of the session.
                        Text { textFormat: Text.PlainText; visible: ctl.active; Layout.fillWidth: true; elide: Text.ElideRight; text: qsTr("Cycle: %1").arg(ctl.cycleText); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
                        Text { textFormat: Text.PlainText; visible: ctl.active; Layout.fillWidth: true; elide: Text.ElideRight; text: qsTr("Current peak: %1").arg(ctl.peakText); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                }

                // Last session.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: recap.implicitHeight + 28
                    ColumnLayout {
                        id: recap
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 4
                        Text { textFormat: Text.PlainText; text: qsTr("Last session"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium }
                        Text {
                            textFormat: Text.PlainText
                            visible: !ctl.recap.title
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: qsTr("Finish a session to see a short recap here."); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        Text { textFormat: Text.PlainText; visible: !!ctl.recap.title; text: ctl.recap.title || ""; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 14; font.weight: Font.Medium; font.capitalization: Font.Capitalize }
                        Text { textFormat: Text.PlainText; visible: !!ctl.recap.title; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.recap.summary || ""; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
                        Text { textFormat: Text.PlainText; visible: !!ctl.recap.title; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: ctl.recap.hint || ""; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11 }
                    }
                }

                // Recent sessions.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: recent.implicitHeight + 28
                    ColumnLayout {
                        id: recent
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: qsTr("Recent Sessions"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium; Layout.fillWidth: true }
                            FlowButton {
                                id: exportBtn
                                property bool saved: false
                                text: saved ? qsTr("Saved ✓") : qsTr("Export CSV")
                                enabled: ctl.history.length > 0
                                onClicked: exportDialog.open()
                                Timer { id: savedTimer; interval: 2000; onTriggered: exportBtn.saved = false }
                            }
                        }
                        Text { textFormat: Text.PlainText; visible: ctl.history.length === 0; text: qsTr("No completed session yet."); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                        Repeater {
                            model: ctl.history.slice(0, 8)
                            delegate: Text {
                                textFormat: Text.PlainText
                                required property var modelData
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: modelData.text; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 11
                            }
                        }
                    }
                }

                // Session log (collapsible).
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: logBox.implicitHeight + 28
                    ColumnLayout {
                        id: logBox
                        x: 14; y: 14; width: parent.width - 28
                        spacing: 6
                        property bool open: false
                        RowLayout {
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: qsTr("Session log"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 15; font.weight: Font.Medium; Layout.fillWidth: true }
                            Icon { name: "chevronDown"; width: 16; height: 16; color: Theme.textDim; rotation: logBox.open ? 180 : 0 }
                            TapHandler { onTapped: logBox.open = !logBox.open }
                        }
                        Text {
                            textFormat: Text.PlainText
                            visible: logBox.open
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: ctl.logLines.length ? ctl.logLines.join("\n") : qsTr("Nothing logged yet.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11
                        }
                    }
                }

                // About.
                GlassCard {
                    Layout.fillWidth: true; Layout.preferredHeight: about.implicitHeight + 28
                    Text {
                        textFormat: Text.PlainText
                        id: about
                        x: 14; y: 14; width: parent.width - 28
                        wrapMode: Text.WordWrap
                        text: qsTr("Standalone up/down cycles for stamina/control training — no video or script required. Classic Stop-Start and Plateau stay available (Preset dropdown + Custom → Technique). Newer multi-phase presets (wave / massage / variable) sit beside them. Vibration and suction are both stepless. Each cycle is logged (Settings → log folder) so drive can be tuned later.")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                    }
                }
            }
        }
    }

    TrainingEditor { id: editor; objectName: "editor"; anchors.fill: parent; controller: ctl }

    FileDialog {
        id: exportDialog
        title: qsTr("Export training history as")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)")]
        currentFile: "file://" + ctl.defaultExportName()
        onAccepted: {
            const err = ctl.exportHistoryCsv(selectedFile.toString().replace(/^file:\/\/(\/(?=[A-Za-z]:))?/, ""))
            if (!err) { exportBtn.saved = true; savedTimer.restart() }
        }
    }
}
