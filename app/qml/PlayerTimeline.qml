import QtQuick
import SamN

// Timeline card: the curve, the channel lanes and the audio level.
GlassCard {
    id: root
    objectName: "timeline"
    required property PlayerController ctrl
    property bool empty
    property real mediaPosition
    property real mediaDuration
    property real playheadPosition

    signal seekRequested(real ms)
    signal tipRequested(var item, real x, real y, string text)

    // Old prefs: 4 was Audio Analysis, 3 was the Scene Map stub (now gone).
    function migratedBottomTab() {
        let t = ctrl.uiValue("player.bottomTab", 0)
        if (t === 4) t = 3
        else if (t === 3) t = 0
        return t
    }
    function showAudioAnalysis() {
        bottomTabs.current = 3
        ctrl.setUiValue("player.bottomTab", 3)
    }
    // 0.7 companion axis names and colours (theme_flow.css axis tokens).
    function axisTitle(axis) {
        return { surge: qsTr("Surge"), sway: qsTr("Sway"), twist: qsTr("Twist"), roll: qsTr("Roll"), pitch: qsTr("Pitch") }[axis] || axis
    }
    function laneTitle(lane) {
        return lane === "vibration" ? qsTr("Vibration") : lane === "suction" ? qsTr("Suction")
             : lane === "stroke" ? qsTr("Stroke") : axisTitle(lane)
    }
    function laneColor(lane) {
        return lane === "vibration" ? Theme.laneVibration : lane === "suction" ? Theme.laneSuction
             : lane === "stroke" ? Theme.orange : axisColor(lane)
    }
    function axisColor(axis) {
        return { surge: "#e5c07b", sway: "#98c379", twist: "#e06c9f", roll: "#61afef", pitch: "#d19a66" }[axis] || Theme.textDim
    }
    // 0.7 axis legend toolbar above the ruler (hidden when only stroke exists).
    readonly property bool showAxisLegend: ctrl.hasScript && ctrl.laneChoices.length > 1
    readonly property int axisLegendBand: showAxisLegend ? 34 : 0  // 28 px chips + 6 px gap

    // Bottom tabs: Timeline · Chapters · Bookmarks · Audio Analysis.
    // Scenes stay in the Generator Editor (D-145); the Player tab was only a redirect.
    FlowTabs {
        id: bottomTabs
        x: 16; y: 9
        model: [qsTr("Timeline"), qsTr("Chapters"), qsTr("Bookmarks"), qsTr("Audio Analysis")]
        current: migratedBottomTab()
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
        Item { width: 1; height: root.axisLegendBand + ruler.height; visible: lanes.visible }
        // 0.7 legend chips: a click picks the lane the main curve shows.
        LaneName {
            objectName: "laneName_stroke"
            name: qsTr("Stroke"); dot: Theme.orange; height: curve.height; visible: ctrl.showStrokeLane
            selectable: ctrl.laneChoices.length > 1; active: selectable && ctrl.activeLane === "stroke"
            onPicked: ctrl.activeLane = "stroke"; onStepped: (d) => ctrl.stepActiveLane(d)
        }
        LaneName {
            objectName: "laneName_vibration"
            name: qsTr("Vibration"); dot: Theme.laneVibration; height: vibLane.height; visible: ctrl.showVibrationLane
            selectable: true; active: ctrl.activeLane === "vibration"
            onPicked: ctrl.activeLane = "vibration"; onStepped: (d) => ctrl.stepActiveLane(d)
        }
        LaneName {
            objectName: "laneName_suction"
            name: qsTr("Suction"); dot: Theme.laneSuction; height: sucLane.height; visible: ctrl.showSuctionLane
            selectable: true; active: ctrl.activeLane === "suction"
            onPicked: ctrl.activeLane = "suction"; onStepped: (d) => ctrl.stepActiveLane(d)
        }
        Repeater {
            model: ctrl.axisLanes
            delegate: LaneName {
                required property string modelData
                objectName: "laneName_" + modelData
                name: root.axisTitle(modelData); dot: root.axisColor(modelData); height: lanes.laneH
                selectable: true; active: ctrl.activeLane === modelData
                onPicked: ctrl.activeLane = modelData; onStepped: (d) => ctrl.stepActiveLane(d)
            }
        }
        LaneName { name: qsTr("Audio Level"); dot: "#93aec4"; height: audioLane.height; visible: audioLane.visible }
        LaneName { name: qsTr("Chapters"); dot: "#2f80ed"; height: chapterStrip.height; visible: chapterStrip.visible && ctrl.showScenes }
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
        readonly property int extraLanes: (vibLane.visible ? 1 : 0) + (sucLane.visible ? 1 : 0) + ctrl.axisLanes.length
        readonly property int laneH: curve.visible ? 34
            : Math.max(34, Math.min(140, (height - fixedH - (audioLane.visible ? audioH : 0)) / Math.max(1, extraLanes)))
        readonly property int fixedH: root.axisLegendBand + ruler.height + (heatBar.visible ? heatBar.height + 4 : 0) + markRow.height
                                      + (chapterStrip.visible ? chapterStrip.height : 0)
        // 0.7 stack: axis legend chips (role=toolbar) before the time ruler.
        Row {
            id: axisLegend
            objectName: "axisLegend"
            width: parent.width; spacing: 6
            visible: root.showAxisLegend
            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("Curve axis")
            Repeater {
                model: axisLegend.visible ? ctrl.laneChoices : []
                delegate: LaneName {
                    required property string modelData
                    objectName: "axisChip_" + modelData
                    chip: true
                    name: root.laneTitle(modelData)
                    dot: root.laneColor(modelData)
                    selectable: true
                    active: ctrl.activeLane === modelData
                    onPicked: ctrl.activeLane = modelData
                    onStepped: (d) => ctrl.stepActiveLane(d)
                }
            }
        }
        Item { width: 1; height: axisLegend.visible ? 6 : 0 }
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
            // The main curve shows the picked lane (0.7 Curve select / legend).
            objectName: "mainCurve"
            lane: ctrl.activeLane; lineColor: root.laneColor(ctrl.activeLane); showGrid: true
            // Sharp Play wave: soft bloom, not a muddy halo.
            lineWidth: 1.55; glow: 4.5; fill: 0.12
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
                onPositionChanged: (m) => root.tipRequested(curve, m.x, m.y, curve.hoverText(m.x))
                onExited: root.tipRequested(null, 0, 0, "")
                onClicked: (m) => root.seekRequested(Math.round(curve.msAtX(m.x)) + ctrl.offsetMs)
            }
            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: curve.empty
                text: qsTr("No script loaded"); color: Theme.textMuted; font.family: Theme.fontSoft
            }
            // Another lane picked: its name on the main curve (the chip beside
            // the curve still reads "Stroke" and brings the stroke back).
            Text {
                objectName: "mainCurveLane"
                textFormat: Text.PlainText
                x: 8; y: 6
                visible: ctrl.activeLane !== "stroke"
                text: root.laneTitle(ctrl.activeLane)
                color: root.laneColor(ctrl.activeLane); font.family: Theme.fontSoft; font.pixelSize: 12; font.bold: true
            }
        }
        CurveItem {
            id: vibLane
            width: parent.width; height: lanes.laneH
            visible: ctrl.showVibrationLane  // D-132: the script's channels, or the device's
            lane: "vibration"; lineColor: Theme.laneVibration
            // 0.7: the picked lane is brighter and thicker.
            lineWidth: ctrl.activeLane === "vibration" ? 1.6 : 1.2; glow: ctrl.activeLane === "vibration" ? 6 : 3; fill: 0.14
            Rectangle { anchors.fill: parent; z: -1; color: Qt.rgba(1, 1, 1, 0.045); visible: ctrl.activeLane === "vibration" }
            viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
            Component.onCompleted: ctrl.attachCurve(vibLane)
            // 0.7: a click on a lane picks it for the main curve. Hover matches the main curve tip.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onPositionChanged: (m) => root.tipRequested(vibLane, m.x, m.y, vibLane.hoverText(m.x))
                onExited: root.tipRequested(null, 0, 0, "")
                onClicked: ctrl.activeLane = "vibration"
            }
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
            lineWidth: ctrl.activeLane === "suction" ? 1.6 : 1.2; glow: ctrl.activeLane === "suction" ? 6 : 3; fill: 0.14
            Rectangle { anchors.fill: parent; z: -1; color: Qt.rgba(1, 1, 1, 0.045); visible: ctrl.activeLane === "suction" }
            viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
            Component.onCompleted: ctrl.attachCurve(sucLane)
            // 0.7: a click on a lane picks it for the main curve. Hover matches the main curve tip.
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onPositionChanged: (m) => root.tipRequested(sucLane, m.x, m.y, sucLane.hoverText(m.x))
                onExited: root.tipRequested(null, 0, 0, "")
                onClicked: ctrl.activeLane = "suction"
            }
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / parent.width)
            }
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
        }
        // 0.7 companion axes (surge … pitch) beside the script: read lanes.
        Repeater {
            model: ctrl.axisLanes
            delegate: CurveItem {
                id: axisLane
                required property string modelData
                objectName: "axisLane_" + modelData
                width: lanes.width; height: lanes.laneH
                lane: modelData; lineColor: root.axisColor(modelData)
                lineWidth: ctrl.activeLane === modelData ? 1.6 : 1.2; glow: ctrl.activeLane === modelData ? 6 : 3; fill: 0.14
                Rectangle { anchors.fill: parent; z: -1; color: Qt.rgba(1, 1, 1, 0.045); visible: ctrl.activeLane === axisLane.modelData }
                viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
                Component.onCompleted: ctrl.attachCurve(axisLane)
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: (m) => root.tipRequested(axisLane, m.x, m.y, axisLane.hoverText(m.x))
                    onExited: root.tipRequested(null, 0, 0, "")
                    onClicked: ctrl.activeLane = axisLane.modelData
                }
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / axisLane.width)
                }
                Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
            }
        }
        AudioLevelItem {
            id: audioLane
            width: parent.width; height: lanes.audioH
            visible: !root.empty && ctrl.audioLane
            source: ctrl.videoUrl
            viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
            offsetMs: ctrl.offsetMs
            playheadMs: ctrl.playheadMs
            label: ""
            Rectangle { width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.08) }
        }
        // Chapter bands (0.7 pb-chapter-bands): one coloured band per
        // chapter on the curve's time scale; click = seek to its start.
        // Thumbnails (PARITY scene strip): 24 low frames across the zoom
        // window when a video is linked. Bands, diamonds and "+" stay above.
        Item {
            id: chapterStrip
            objectName: "chapterStrip"
            width: parent.width
            readonly property int bandH: 30
            // 0.7 strip thumbs were 72 px high; keep the row a bit shorter than
            // the decode height so PreserveAspectCrop still looks sharp.
            readonly property int thumbH: 48
            height: sceneThumbStrip.visible ? bandH + thumbH : bandH
            // 0.7 pb.scenes: off by default in the Player.
            visible: ctrl.hasScript && ctrl.showScenes
            clip: true
            readonly property var colors: [Qt.rgba(47 / 255, 128 / 255, 237 / 255, 0.45), Qt.rgba(242 / 255, 140 / 255, 40 / 255, 0.45),
                                           Qt.rgba(155 / 255, 81 / 255, 224 / 255, 0.45), Qt.rgba(39 / 255, 174 / 255, 160 / 255, 0.45),
                                           Qt.rgba(86 / 255, 140 / 255, 220 / 255, 0.38)]
            readonly property var solid: ["#2f80ed", "#f28c28", "#9b51e0", "#27aea0", "#568cdc"]
            readonly property real span: Math.max(1, ctrl.viewEndMs - ctrl.viewStartMs)
            // Power-of-two grid (same idea as the Generator frame strip): zooming
            // lands on the same FrameCache keys again instead of new seeks.
            readonly property real thumbGrid: {
                const step = Math.max(1, span / 24)
                return Math.pow(2, Math.floor(Math.log2(step)))
            }
            // Centre of slot i in [viewStartMs, viewEndMs], snapped and clamped.
            function thumbMs(i) {
                const raw = ctrl.viewStartMs + (i + 0.5) * span / 24
                const snapped = Math.round(raw / thumbGrid) * thumbGrid
                const hi = root.mediaDuration > 0 ? root.mediaDuration - 1 : snapped
                return Math.min(Math.max(0, snapped), Math.max(0, hi))
            }
            Row {
                id: sceneThumbStrip
                objectName: "sceneThumbStrip"
                z: 0
                y: chapterStrip.bandH
                width: parent.width; height: chapterStrip.thumbH
                spacing: 0
                visible: ctrl.hasScript && ctrl.showScenes && ctrl.videoPath !== ""
                Repeater {
                    model: sceneThumbStrip.visible ? 24 : 0
                    delegate: Image {
                        required property int index
                        objectName: "sceneThumb_" + index
                        readonly property real ms: chapterStrip.thumbMs(index)
                        // Place each slot. A Row does not lay these Repeater
                        // delegates out, so a click would always hit the last one.
                        x: index * width
                        y: 0
                        width: sceneThumbStrip.width / 24
                        height: sceneThumbStrip.height
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.height: 72
                        source: "image://frames/" + Math.round(ms) + "/72/" + encodeURIComponent(ctrl.videoPath)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.seekRequested(parent.ms + ctrl.offsetMs)
                        }
                    }
                }
            }
            Repeater {
                model: ctrl.chapterBands
                delegate: Rectangle {
                    required property var modelData
                    z: 1
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
                        onPositionChanged: (m) => root.tipRequested(parent, m.x, m.y, modelData.tip)
                        onExited: root.tipRequested(null, 0, 0, "")
                        onClicked: root.seekRequested(modelData.startMs + ctrl.offsetMs)
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
                z: 1
                anchors.left: parent.left; anchors.leftMargin: 8
                y: (chapterStrip.bandH - implicitHeight) / 2
                visible: ctrl.chapterBands.length === 0
                text: qsTr("No chapters yet — + adds a scene marker at the playhead.")
                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            // "+" (0.7 pb-scene-add): a scene marker at the playhead.
            Rectangle {
                id: sceneAdd
                z: 2
                anchors.right: parent.right
                y: (chapterStrip.bandH - height) / 2
                width: 26; height: 26; radius: 13
                color: addArea.containsMouse ? Qt.rgba(0, 217 / 255, 244 / 255, 0.3) : "#0a2340"
                border.width: 1; border.color: Theme.cyan
                Icon { anchors.centerIn: parent; name: "plus"; color: Theme.cyan; width: 14; height: 14 }
                MouseArea {
                    id: addArea
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: (m) => root.tipRequested(sceneAdd, m.x, m.y, qsTr("Add a scene marker at the playhead (starts a chapter)"))
                    onExited: root.tipRequested(null, 0, 0, "")
                    onClicked: ctrl.addSceneMarker(ctrl.playheadMs + ctrl.offsetMs)
                }
            }
        }
        Item { width: 1; height: 4; visible: heatBar.visible }
        // Heatmap: click = seek, drag = mark (0.7). Taller bar (0.5.45)
        // so mark/align is usable; wheel zooms like the curve lanes.
        HeatmapItem {
            id: heatBar
            objectName: "heatBar"
            width: parent.width; height: 28
            opacity: 0.9
            visible: !root.empty
            viewStartMs: ctrl.viewStartMs; viewEndMs: ctrl.viewEndMs
            markerStartMs: dragArea.dragging ? Math.min(dragArea.fromMs, dragArea.toMs) : ctrl.markerStartMs
            markerEndMs: dragArea.dragging ? Math.max(dragArea.fromMs, dragArea.toMs) : ctrl.markerEndMs
            Component.onCompleted: ctrl.attachHeatmap(heatBar)
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (e) => ctrl.zoomAt(e.angleDelta.y > 0 ? 1.25 : 1 / 1.25, point.position.x / heatBar.width)
            }
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
                        root.tipRequested(null, 0, 0, "")
                    } else {
                        root.tipRequested(heatBar, m.x, m.y, ctrl.heatTooltip(msAt(m.x)))
                    }
                }
                onReleased: (m) => { dragging = false; root.seekRequested(ctrl.finishHeatmapDrag(fromMs, msAt(m.x))) }
                onExited: root.tipRequested(null, 0, 0, "")
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
            HelpChip { key: "pb-chapter-add"; anchors.verticalCenter: parent.verticalCenter }
            HelpChip { key: "pb-chapter-hint"; anchors.verticalCenter: parent.verticalCenter }
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
            onSeek: (i) => root.seekRequested(ctrl.chapterVideoMs(i))
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
                onClicked: if (ctrl.addBookmark(bookmarkName.text, root.playheadPosition)) bookmarkName.text = ""
            }
            HelpChip { key: "pb-bookmark-add"; anchors.verticalCenter: parent.verticalCenter }
            HelpChip { key: "pb-bookmark-hint"; anchors.verticalCenter: parent.verticalCenter }
        }
        MarkList {
            anchors.top: bookmarkAdd.bottom; anchors.topMargin: 10
            width: parent.width; height: parent.height - bookmarkAdd.height - 10
            model: ctrl.bookmarks
            emptyText: qsTr("No bookmarks yet.")
            onSeek: (i) => root.seekRequested(ctrl.bookmarkVideoMs(i))
            onRename: (i, name) => ctrl.renameBookmark(i, name)
            onRemove: (i) => ctrl.removeBookmark(i)
        }
    }
    // Audio Analysis: BPM readout and the whole-video waveform (click seeks).
    Item {
        visible: bottomTabs.current === 3
        x: 16; y: 46; width: parent.width - 32; height: parent.height - 58
        Row {
            id: bpmRow
            spacing: 10
            Text {
                objectName: "audioAnalysisBpm"
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: ctrl.effectiveBpm > 0 ? qsTr("%1 BPM").arg(ctrl.effectiveBpm) : qsTr("— BPM")
                color: Theme.text; font.family: Theme.font; font.pixelSize: 14
            }
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Set BPM and Sound Curve in the Inspector → Audio tab.")
                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
            }
        }
        AudioLevelItem {
            id: wholeAudio
            anchors.top: bpmRow.bottom; anchors.topMargin: 10
            width: parent.width; height: 72
            source: ctrl.videoUrl
            viewStartMs: 0; viewEndMs: root.mediaDuration > 0 ? root.mediaDuration : -1
            playheadMs: root.mediaPosition
            beatPeriodMs: (ctrl.bpmGrid && ctrl.effectiveBpm > 0) ? (60000.0 / ctrl.effectiveBpm) : 0
            label: ""
            MouseArea {
                anchors.fill: parent
                onClicked: (m) => root.seekRequested(Math.round(m.x / width * root.mediaDuration))
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
