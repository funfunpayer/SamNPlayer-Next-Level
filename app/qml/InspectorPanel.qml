import QtQuick
import SamN

// The Inspector of the Player (0.7 v1): tabs Devices · Audio · Playback ·
// View. Functions not built yet say which phase brings them; nothing pretends.
GlassCard {
    id: root
    required property PlayerController ctrl
    property bool collapsed: false
    property alias tab: tabs.current
    signal seekRequested(real videoMs)
    signal addToListRequested()
    // The video position (for Save project) and the project picker (the page owns the dialog).
    property real videoPositionMs: 0
    signal projectLoadRequested()
    property string projectStatus: ""
    width: collapsed ? 44 : 320
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    component Card: Rectangle {
        default property alias content: body.data
        property string title
        width: parent ? parent.width : 280
        height: body.implicitHeight + head.height + 22
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 1, 1, 0.025); border.color: Theme.cardBorder
        Text {
            id: head
            x: 12; y: 10; text: parent.title
            color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        Column { id: body; x: 12; y: head.y + head.height + 8; width: parent.width - 24; spacing: 8 }
    }
    component Note: Text {
        width: parent ? parent.width : 260
        wrapMode: Text.Wrap
        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
    }
    component Channel: Item {
        property string name
        property color dot
        property int value
        width: parent ? parent.width : 260; height: 26
        Rectangle { y: 9; width: 8; height: 8; radius: 4; color: parent.dot }
        Text { x: 16; anchors.verticalCenter: parent.verticalCenter; text: parent.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
        Rectangle {
            x: 100; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 150; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle { width: parent.width * parent.parent.value / 100; height: parent.height; radius: 3; color: parent.parent.dot
                Behavior on width { NumberAnimation { duration: 90 } } }
        }
        Text {
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: root.ctrl.hasScript ? parent.value : "–"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
    }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Icon { anchors.centerIn: parent; name: root.collapsed ? "chevronLeft" : "chevronRight"; width: 16; height: 16; color: Theme.textDim }
        MouseArea { id: tg; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.collapsed = !root.collapsed }
    }
    Text {
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

            // Devices: live channels. Output to a device comes with P2.
            Column {
                id: devices
                visible: tabs.current === 0
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Channels")
                    Channel { name: qsTr("Stroke"); dot: Theme.orange; value: root.ctrl.strokeNow }
                    Channel { name: qsTr("Vibration"); dot: Theme.laneVibration; value: root.ctrl.vibrationNow }
                    Channel { name: qsTr("Suction"); dot: Theme.laneSuction; value: root.ctrl.suctionNow }
                    Note { text: qsTr("Live values at the playhead. Sending them to a device (levels, effects, stroker range) comes with the devices phase (P2).") }
                }
            }
            // Audio: BPM. Vibration from the video's sound comes with P2.
            Column {
                id: audio
                visible: tabs.current === 1
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("BPM")
                    Row {
                        spacing: 8
                        FlowTextField {
                            width: 90; placeholder: root.ctrl.bpm > 0 ? String(root.ctrl.bpm) : qsTr("20–240")
                            onAccepted: root.ctrl.bpm = parseFloat(text) || 0
                        }
                        FlowButton { text: qsTr("BPM grid"); checked: root.ctrl.bpmGrid; onClicked: root.ctrl.bpmGrid = !root.ctrl.bpmGrid }
                    }
                    Note { text: qsTr("Beat lines on the curve (every 4th is a bar). Display only; the script is never changed. Detecting the BPM from the sound comes with the audio tempo port (P3).") }
                }
                Card {
                    title: qsTr("Audio Vibration")
                    Note { text: qsTr("Vibration from the video's sound plays on a device, so it comes with the devices phase (P2).") }
                }
            }
            // Playback: script tools.
            Column {
                id: playback
                visible: tabs.current === 2
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Script Tools")
                    Row {
                        spacing: 8
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Script offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        OffsetField { value: root.ctrl.offsetMs; onChanged: (v) => root.ctrl.offsetMs = v }
                    }
                    Row {
                        spacing: 8
                        FlowButton { text: qsTr("Reset"); enabled: root.ctrl.offsetMs !== 0; onClicked: root.ctrl.offsetMs = 0 }
                        FlowButton { text: qsTr("Bake offset"); enabled: false }
                    }
                    Note { text: qsTr("Positive: the script comes later. Saved per script. Baking it into the script's times (all channels, bookmarks and chapters in one safe step) comes with the Editor (P4).") }
                }
                // Play list (0.7 pb-playlist): shown with 2+ scripts.
                Card {
                    title: qsTr("Playlist")
                    Flow {
                        width: parent.width; spacing: 8
                        FlowButton {
                            text: qsTr("Add to list"); icon: "plus"; enabled: root.ctrl.hasScript
                            HoverHandler { id: addHover }
                            ToolTipText { shown: addHover.hovered; text: qsTr("Add another Emotion Script to the list") }
                            onClicked: root.addToListRequested()
                        }
                        FlowButton { text: qsTr("Auto-advance"); checked: root.ctrl.playlistAuto; onClicked: root.ctrl.playlistAuto = !root.ctrl.playlistAuto }
                        FlowButton { text: qsTr("Shuffle"); icon: "shuffle"; checked: root.ctrl.playlistShuffle; onClicked: root.ctrl.playlistShuffle = !root.ctrl.playlistShuffle }
                        FlowButton { text: qsTr("Repeat playlist"); icon: "repeat"; checked: root.ctrl.playlistRepeat; onClicked: root.ctrl.playlistRepeat = !root.ctrl.playlistRepeat }
                    }
                    Note { visible: !root.ctrl.playlistVisible; text: qsTr("Drop several scripts or add one to play them in a row.") }
                    Column {
                        visible: root.ctrl.playlistVisible
                        width: parent.width; spacing: 4
                        Repeater {
                            model: root.ctrl.playlist
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: parent.width; height: 32; radius: 8
                                color: modelData.active ? Qt.rgba(0, 120 / 255, 1, 0.22) : (rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                                border.width: modelData.active ? 1 : 0; border.color: Theme.cyan
                                Text {
                                    x: 10; anchors.verticalCenter: parent.verticalCenter
                                    text: index + 1; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                                Text {
                                    x: 32; width: parent.width - 32 - 30; anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.name; elide: Text.ElideRight
                                    color: modelData.active ? "white" : Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                MouseArea {
                                    id: rowArea
                                    anchors.fill: parent; anchors.rightMargin: 30
                                    hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.ctrl.pickPlaylist(index)
                                }
                                IconButton {
                                    anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter
                                    width: 26; height: 26; icon: "close"; tip: qsTr("Remove")
                                    onClicked: root.ctrl.removeFromPlaylist(index)
                                }
                            }
                        }
                    }
                }
            }
            // View: what the timeline shows.
            Column {
                id: view
                visible: tabs.current === 3
                width: parent.width; spacing: 10
                Card {
                    title: qsTr("Timeline")
                    Row {
                        spacing: 8
                        FlowButton {
                            text: qsTr("Audio level lane"); checked: root.ctrl.audioLane
                            onClicked: root.ctrl.audioLane = !root.ctrl.audioLane
                        }
                        FlowButton {
                            text: qsTr("0–100 on video"); checked: root.ctrl.posOverlay
                            onClicked: root.ctrl.posOverlay = !root.ctrl.posOverlay
                        }
                    }
                    // Speed highlights (0.7 pb-speed-hl): red bands over too fast strokes.
                    Row {
                        spacing: 10
                        FlowButton {
                            text: qsTr("Speed highlights"); checked: root.ctrl.speedHighlights
                            onClicked: root.ctrl.speedHighlights = !root.ctrl.speedHighlights
                            HoverHandler { id: hlHover }
                            ToolTipText { shown: hlHover.hovered; text: qsTr("Paint red “too fast” bands on the curve (OFS community intensity). Display only — separate from Speed-cap edit.") }
                        }
                        FlowSlider {
                            anchors.verticalCenter: parent.verticalCenter; width: 76
                            enabled: root.ctrl.speedHighlights
                            from: 150; to: 800; value: root.ctrl.speedHighlightThresh
                            onMoved: (v) => root.ctrl.speedHighlightThresh = Math.round(v / 25) * 25
                            HoverHandler { id: hlThHover }
                            ToolTipText { shown: hlThHover.hovered; text: qsTr("Intensity threshold for Speed highlights on the curve (500×|Δpos|/|Δt|). Lower = more red bands.") }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: Math.round(root.ctrl.speedHighlightThresh); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                    Note { text: qsTr("Heatmap: click = seek, drag = mark. Wheel on the curve zooms.") }
                }
                // Project & heatmap export (0.7 Tools "Project & heatmap export").
                Card {
                    title: qsTr("Project & heatmap export")
                    Flow {
                        width: parent.width
                        spacing: 8
                        FlowButton {
                            text: qsTr("Heatmap PNG"); enabled: root.ctrl.hasScript
                            HoverHandler { id: hmHover }
                            ToolTipText { shown: hmHover.hovered; text: qsTr("Intensity heatmap as PNG (chapters as ticks)") }
                            onClicked: {
                                const path = root.ctrl.exportHeatmapPng()
                                root.projectStatus = path !== "" ? qsTr("Heatmap: %1").arg(path) : root.ctrl.lastError
                            }
                        }
                        FlowButton {
                            text: qsTr("Save project"); enabled: root.ctrl.hasScript
                            HoverHandler { id: spHover }
                            ToolTipText { shown: spHover.hovered; text: qsTr("Save video+script+offset as .snp.json") }
                            onClicked: {
                                const path = root.ctrl.saveProject(Math.round(root.videoPositionMs))
                                root.projectStatus = path !== "" ? qsTr("Project: %1").arg(path) : root.ctrl.lastError
                            }
                        }
                        FlowButton {
                            text: qsTr("Load project")
                            HoverHandler { id: lpHover }
                            ToolTipText { shown: lpHover.hovered; text: qsTr("Open a .snp.json project (script, video, offset, seek, loop)") }
                            onClicked: root.projectLoadRequested()
                        }
                    }
                    Note { visible: root.projectStatus !== ""; text: root.projectStatus; wrapMode: Text.WrapAnywhere }
                }
                // O-markers (0.7 View): from the marked range on the heatmap.
                Card {
                    title: qsTr("O-markers")
                    FlowCombo {
                        id: oKind
                        width: parent.width
                        model: [qsTr("Primary (peak)"), qsTr("Secondary (earlier, softer)")]
                    }
                    Row {
                        visible: oKind.currentIndex === 1
                        spacing: 10
                        Text { anchors.verticalCenter: parent.verticalCenter; text: qsTr("Intensity"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        FlowSlider {
                            anchors.verticalCenter: parent.verticalCenter; width: 130
                            from: 0; to: 1; value: root.ctrl.oMarkerIntensity
                            onMoved: (v) => root.ctrl.oMarkerIntensity = Math.round(v * 20) / 20
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: Math.round(root.ctrl.oMarkerIntensity * 100) + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                    FlowButton {
                        text: qsTr("Apply as O-marker"); enabled: root.ctrl.hasMarker
                        onClicked: root.ctrl.addOMarkerFromSelection(oKind.currentIndex === 1 ? "secondary" : "primary")
                    }
                    Note { visible: !root.ctrl.hasMarker; text: qsTr("Mark a range on the heatmap first.") }
                    MarkList {
                        width: parent.width
                        height: Math.min(200, Math.max(30, count * 38))
                        model: root.ctrl.oMarkers
                        canRename: false
                        emptyText: qsTr("No O-markers yet.")
                        onSeek: (i) => root.seekRequested(root.ctrl.oMarkers[i].startMs + root.ctrl.offsetMs)
                        onRemove: (i) => root.ctrl.removeOMarker(i)
                    }
                }
                Card {
                    title: qsTr("Curve editing")
                    Note { text: qsTr("Editing dots, range tools and Undo come with the Editor in the Generator mode (P3–P4).") }
                }
            }
        }
    }
}
