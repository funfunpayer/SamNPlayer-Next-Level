import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SamN

// The AI Train label editor (D-155 §2.2): each label is a track through the
// clip. Scrub or play; the boxes ride along on the frame. Drag on the frame to
// set the current label's box at this frame (a confirmed key); where a box
// drifts, drag it back. Lanes under the frame show where each label is, where
// it is confirmed and where it is not visible.
Item {
    id: root
    required property LabelController labels
    property bool active: true
    // 0.7's tag colours, in label order.
    readonly property var colors: ["#3dccc0", "#f2b03d", "#5ecf8a", "#e8eaed", "#7aa2ff", "#ff7a9a", "#c9a0ff", "#9ad6ff", "#ffd27a"]
    function colorOf(i) { return colors[i % colors.length] }
    // Exclusion zones ("not this": a thigh, a foreign hand, a second person)
    // are red and hatched, whatever their place in the list.
    readonly property color excludeColor: "#ff5a5a"
    function toneOf(m) { return m && m.cls === "exclude" ? excludeColor : colorOf(m ? m.classIndex : 0) }
    function fmt(ms) { return Math.floor(ms / 60000) + ":" + String(Math.floor(ms / 1000) % 60).padStart(2, "0") + "." + String(Math.floor(ms / 100) % 10) }
    readonly property int currentIndex: labels.labels.indexOf(labels.currentLabel)
    readonly property var currentLane: currentIndex >= 0 ? labels.lanes[currentIndex] : null
    // "hand" reads "hand 1" once there is a "hand 2".
    function displayName(lane) {
        if (lane.label !== lane.cls) return lane.label
        return labels.lanes.filter((l) => l.cls === lane.cls).length > 1 ? lane.cls + " 1" : lane.cls
    }

    // Play: steps through the frames (scrubbing speed, no sound).
    property bool playing: false
    Timer {
        interval: 80; repeat: true; running: root.playing && root.active
        onTriggered: {
            if (labels.previewMs >= labels.durationMs) { root.playing = false; return }
            labels.seek(labels.previewMs + interval)
        }
    }

    FolderDialog {
        id: exportDialog
        title: qsTr("Folder for the samples")
        onAccepted: labels.exportDataset(selectedFolder.toString(), everyMs.value)
    }

    Shortcut { sequence: "Space"; enabled: root.active && labels.hasVideo; onActivated: root.playing = !root.playing }
    Shortcut { sequence: ","; enabled: root.active && labels.hasVideo; onActivated: { root.playing = false; labels.step(-1) } }
    Shortcut { sequence: "."; enabled: root.active && labels.hasVideo; onActivated: { root.playing = false; labels.step(1) } }
    Shortcut { sequence: "Left"; enabled: root.active && labels.hasVideo; onActivated: labels.seek(labels.previewMs - 1000) }
    Shortcut { sequence: "Right"; enabled: root.active && labels.hasVideo; onActivated: labels.seek(labels.previewMs + 1000) }
    Shortcut { sequence: "X"; enabled: root.active && labels.hasVideo; onActivated: labels.markAbsent(false) }
    Shortcut { sequence: "H"; enabled: root.active && labels.hasVideo; onActivated: labels.markAbsent(true) }
    Shortcut { sequences: [StandardKey.Delete, "Backspace"]; enabled: root.active && labels.hasVideo; onActivated: labels.removeKey() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: root.active && labels.canUndo; onActivated: labels.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; enabled: root.active && labels.canRedo; onActivated: labels.redo() }
    Repeater {
        model: 9
        delegate: Item {
            required property int index
            Shortcut {
                sequence: String(index + 1)
                enabled: root.active && index < labels.labels.length
                onActivated: labels.currentLabel = labels.labels[index]
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12

        // ---- labels ----
        GlassCard {
            Layout.preferredWidth: 230; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Labels"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                Repeater {
                    model: labels.lanes
                    delegate: Rectangle {
                        required property int index
                        required property var modelData
                        objectName: "labelRow_" + modelData.label
                        Layout.fillWidth: true; height: 32; radius: 9
                        readonly property bool current: modelData.label === labels.currentLabel
                        color: current ? Qt.rgba(0, 120 / 255, 1, 0.22) : la.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                        border.color: current ? Theme.cyan : "transparent"
                        Rectangle { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 12; height: 12; radius: modelData.cls === "contact" ? 6 : 3; color: root.toneOf(modelData) }
                        Text {
                            textFormat: Text.PlainText
                            x: 30; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 64; elide: Text.ElideRight
                            text: (index < 9 ? (index + 1) + "  " : "") + root.displayName(modelData)
                            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                        MouseArea { id: la; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: labels.currentLabel = modelData.label }
                        // One more object of this class (a second hand, the other breast …).
                        Rectangle {
                            objectName: "addObject_" + modelData.label
                            anchors.right: parent.right; anchors.rightMargin: 6; anchors.verticalCenter: parent.verticalCenter
                            width: 22; height: 22; radius: 11
                            color: pa.containsMouse ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                            Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: "+"; color: Theme.textDim; font.pixelSize: 15 }
                            MouseArea { id: pa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: labels.addLabel(modelData.cls) }
                            ToolTipText { text: qsTr("Another %1").arg(modelData.cls); shown: pa.containsMouse }
                        }
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    visible: labels.labels.length === 0
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: qsTr("Pick what to mark below, then drag its box on the frame. A class can have several objects: + adds another (a second hand, the other breast).")
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                Text { textFormat: Text.PlainText; text: qsTr("Mark"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11; topPadding: 4 }
                Flow {
                    Layout.fillWidth: true; spacing: 4
                    Repeater {
                        model: labels.presetLabels
                        delegate: Rectangle {
                            required property string modelData
                            objectName: "addLabel_" + modelData
                            height: 24; width: chipText.implicitWidth + 16; radius: 12
                            color: ca.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.03)
                            border.color: Theme.cardBorder
                            Text { id: chipText; textFormat: Text.PlainText; anchors.centerIn: parent; text: "+ " + (modelData === "exclude" ? qsTr("not this (exclude)") : modelData); color: modelData === "exclude" ? root.excludeColor : Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                            MouseArea { id: ca; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: labels.addLabel(modelData) }
                        }
                    }
                }
                FlowTextField {
                    id: custom
                    Layout.fillWidth: true
                    placeholder: qsTr("Own class, Enter adds it")
                    onAccepted: { if (labels.addLabel(text)) text = "" }
                }
                Item { Layout.fillHeight: true }
                FlowButton { Layout.fillWidth: true; text: qsTr("Not in the picture (X)"); enabled: labels.currentLabel !== ""; onClicked: labels.markAbsent(false) }
                FlowButton { objectName: "markCovered"; Layout.fillWidth: true; text: qsTr("There but covered (H)"); enabled: labels.currentLabel !== ""; onClicked: labels.markAbsent(true) }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    text: qsTr("Partly visible: mark the part you see. Covered stretches are left out of training.")
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                }
                FlowButton { Layout.fillWidth: true; text: qsTr("Remove this key (Del)"); enabled: labels.currentLabel !== ""; onClicked: labels.removeKey() }
                FlowButton { Layout.fillWidth: true; text: qsTr("Confirm all of this label"); enabled: labels.currentLabel !== ""; onClicked: labels.confirmAll() }
            }
        }

        // ---- frame, transport, lanes ----
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 10

            Rectangle {
                id: stage
                objectName: "labelStage"
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: Theme.radius; color: "#02070f"; clip: true
                FrameItem {
                    id: frameView
                    objectName: "labelFrame"
                    anchors.fill: parent
                    image: labels.frame
                }
                readonly property real scaleX: labels.videoSize.width > 0 ? frameView.imageRect.width / labels.videoSize.width : 1
                readonly property real scaleY: labels.videoSize.height > 0 ? frameView.imageRect.height / labels.videoSize.height : 1
                Repeater {
                    model: labels.boxes
                    delegate: Rectangle {
                        required property var modelData
                        readonly property color tone: root.toneOf(modelData)
                        readonly property bool current: modelData.label === labels.currentLabel
                        visible: modelData.present && !(draw.active && current)
                        x: frameView.imageRect.x + modelData.x * stage.scaleX
                        y: frameView.imageRect.y + modelData.y * stage.scaleY
                        width: modelData.w * stage.scaleX; height: modelData.h * stage.scaleY
                        color: Qt.rgba(tone.r, tone.g, tone.b, current ? 0.18 : 0.08)
                        border.color: tone; border.width: current ? 2.5 : 1.5
                        // Dashed look for what nobody has confirmed yet: a lighter border.
                        opacity: modelData.reviewed || modelData.confirmed ? 1 : 0.6
                        Text {
                            textFormat: Text.PlainText
                            x: 4; y: 2
                            text: (modelData.cls === "exclude" ? qsTr("not this · ") : "") + modelData.label + (modelData.keyed ? " ◆" : "")
                            color: parent.tone; font.family: Theme.fontSoft; font.pixelSize: 11
                        }
                        // An exclusion zone is hatched: the tracker stays out.
                        Item {
                            visible: modelData.cls === "exclude"
                            anchors.fill: parent; anchors.margins: 1; clip: true
                            Repeater {
                                model: modelData.cls === "exclude" ? Math.ceil((parent.width + parent.height) / 12) : 0
                                delegate: Rectangle {
                                    required property int index
                                    x: index * 12 - parent.height; y: parent.height / 2 - height / 2
                                    width: 2; height: parent.height * 1.5
                                    rotation: 45
                                    color: Qt.rgba(1, 0.35, 0.35, 0.35)
                                }
                            }
                        }
                        // The contact point: where the stroke touches (vibration).
                        Rectangle { visible: modelData.cls === "contact"; anchors.centerIn: parent; width: 18; height: 2; color: parent.tone }
                        Rectangle { visible: modelData.cls === "contact"; anchors.centerIn: parent; width: 2; height: 18; color: parent.tone }
                    }
                }
                Rectangle {
                    visible: draw.active
                    x: frameView.imageRect.x + draw.rect.x * stage.scaleX
                    y: frameView.imageRect.y + draw.rect.y * stage.scaleY
                    width: draw.rect.width * stage.scaleX; height: draw.rect.height * stage.scaleY
                    readonly property color tone: root.toneOf(root.currentLane)
                    color: Qt.rgba(tone.r, tone.g, tone.b, 0.18)
                    border.color: tone; border.width: 2
                }
                // Drag to draw the current label's box here; drag inside it to move it.
                MouseArea {
                    id: draw
                    objectName: "labelDraw"
                    anchors.fill: parent
                    enabled: labels.hasVideo && labels.currentLabel !== ""
                    cursorShape: Qt.CrossCursor
                    property bool active: false
                    property bool moving: false
                    property point start
                    property rect rect
                    property rect origin
                    function toVideo(mx, my) { return Qt.point((mx - frameView.imageRect.x) / stage.scaleX, (my - frameView.imageRect.y) / stage.scaleY) }
                    function currentBox() {
                        const b = labels.boxes[root.currentIndex]
                        return b && b.present ? Qt.rect(b.x, b.y, b.w, b.h) : Qt.rect(0, 0, 0, 0)
                    }
                    onPressed: (m) => {
                        root.playing = false
                        start = toVideo(m.x, m.y)
                        origin = currentBox()
                        moving = origin.width > 0 && start.x >= origin.x && start.x <= origin.x + origin.width
                                 && start.y >= origin.y && start.y <= origin.y + origin.height
                        rect = moving ? origin : Qt.rect(start.x, start.y, 0, 0)
                        active = true
                    }
                    onPositionChanged: (m) => {
                        const p = toVideo(m.x, m.y)
                        rect = moving ? Qt.rect(origin.x + p.x - start.x, origin.y + p.y - start.y, origin.width, origin.height)
                                      : Qt.rect(Math.min(start.x, p.x), Math.min(start.y, p.y), Math.abs(p.x - start.x), Math.abs(p.y - start.y))
                    }
                    onReleased: { active = false; labels.setBox(rect) }
                }
                Text {
                    textFormat: Text.PlainText
                    anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 10
                    text: root.fmt(labels.previewMs) + " / " + root.fmt(labels.durationMs)
                    color: "white"; font.family: Theme.fontSoft; font.pixelSize: 12
                }
            }

            // Transport: step, play, step; the time.
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                IconButton { icon: "skipBack"; tip: qsTr("Start"); onClicked: labels.seek(0) }
                IconButton { icon: "back10"; tip: qsTr("One frame back (,)"); onClicked: { root.playing = false; labels.step(-1) } }
                PlayButton { objectName: "labelPlay"; width: 46; height: 46; playing: root.playing; onClicked: root.playing = !root.playing }
                IconButton { icon: "forward10"; tip: qsTr("One frame on (.)"); onClicked: { root.playing = false; labels.step(1) } }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: labels.status
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                Text { textFormat: Text.PlainText; text: qsTr("Sample every"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                FlowSlider { id: everyMs; objectName: "sampleEvery"; from: 100; to: 4000; value: 500; Layout.preferredWidth: 110 }
                Text { textFormat: Text.PlainText; text: Math.round(everyMs.value) + " ms"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                FlowButton { objectName: "exportSamples"; primary: true; text: qsTr("Export samples…"); enabled: labels.labels.length > 0; onClicked: exportDialog.open() }
            }

            // Lanes: one per label; click or drag to scrub.
            GlassCard {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(1, labels.lanes.length) * 30 + 24
                Item {
                    id: lanesArea
                    anchors.fill: parent; anchors.margins: 12
                    readonly property real nameW: 90
                    readonly property real span: Math.max(1, labels.durationMs)
                    function xOf(ms) { return nameW + (width - nameW) * Math.max(0, Math.min(1, ms / span)) }
                    Column {
                        anchors.fill: parent; spacing: 4
                        Repeater {
                            model: labels.lanes
                            delegate: Item {
                                id: lane
                                required property var modelData
                                width: lanesArea.width; height: 26
                                readonly property color tone: root.toneOf(modelData)
                                Text {
                                    textFormat: Text.PlainText
                                    width: lanesArea.nameW - 8; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideRight
                                    text: root.displayName(lane.modelData)
                                    color: lane.modelData.label === labels.currentLabel ? Theme.text : Theme.textDim
                                    font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                                Rectangle { x: lanesArea.nameW; width: lanesArea.width - lanesArea.nameW; height: parent.height; radius: 5; color: Qt.rgba(1, 1, 1, 0.04) }
                                Repeater {
                                    model: lane.modelData.spans
                                    delegate: Rectangle {
                                        required property var modelData
                                        x: lanesArea.xOf(modelData.startMs); y: 4
                                        width: Math.max(2, lanesArea.xOf(modelData.endMs) - x); height: lane.height - 8; radius: 3
                                        color: modelData.present ? Qt.rgba(lane.tone.r, lane.tone.g, lane.tone.b, modelData.reviewed ? 0.55 : 0.22)
                                                                 : Qt.rgba(1, 1, 1, modelData.reviewed ? 0.10 : 0.04)
                                        // Covered: a dashed outline, it is there but not seen.
                                        border.color: modelData.covered ? Qt.rgba(lane.tone.r, lane.tone.g, lane.tone.b, 0.7) : "transparent"
                                        border.width: modelData.covered ? 1 : 0
                                    }
                                }
                                Repeater {
                                    model: lane.modelData.keys
                                    delegate: Rectangle {
                                        required property var modelData
                                        x: lanesArea.xOf(modelData.ms) - 5; y: lane.height / 2 - 5
                                        width: 10; height: 10; radius: modelData.absent ? 2 : 5; rotation: modelData.absent ? 45 : 0
                                        color: modelData.confirmed ? lane.tone : "transparent"
                                        border.color: lane.tone; border.width: 1.5
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        x: lanesArea.xOf(labels.previewMs); width: 2; height: parent.height
                        color: "white"; opacity: 0.85
                    }
                    MouseArea {
                        x: lanesArea.nameW; width: parent.width - lanesArea.nameW; height: parent.height
                        function seekAt(mx) { labels.seek(Math.max(0, Math.min(1, mx / width)) * lanesArea.span) }
                        onPressed: (m) => { root.playing = false; seekAt(m.x) }
                        onPositionChanged: (m) => { if (pressed) seekAt(m.x) }
                    }
                }
            }
        }
    }
}
