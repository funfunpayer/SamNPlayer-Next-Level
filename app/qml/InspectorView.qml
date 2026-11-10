import QtQuick
import QtQuick.Dialogs
import SamN

Column {
    id: root
    required property var ctrl
    required property var device
    required property Item rootPanel
    width: parent ? parent.width : 280
    spacing: 10
    InspectorCard {
        title: qsTr("Timeline")
        help: "pb-marker-hint"
        Row {
            spacing: 8
            FlowButton {
                text: qsTr("Audio level lane"); checked: ctrl.audioLane
                onClicked: ctrl.audioLane = !ctrl.audioLane
            }
            HelpChip { key: "pb-audio-lane-row"; anchors.verticalCenter: parent.verticalCenter }
            FlowButton {
                objectName: "showScenes"
                text: qsTr("Scene markers"); checked: ctrl.showScenes
                onClicked: ctrl.showScenes = !ctrl.showScenes
            }
            HelpChip { key: "pb-scenes-row"; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 8
            FlowButton {
                text: qsTr("0–100 on video"); checked: ctrl.posOverlay
                onClicked: ctrl.posOverlay = !ctrl.posOverlay
            }
            HelpChip { key: "pb-pos-overlay-row"; anchors.verticalCenter: parent.verticalCenter }
        }
        // 0.7 "Show contact areas" with its hint, when the script stores marks.
        Row {
            spacing: 8
            visible: ctrl.contactMarks.hint !== undefined
            FlowButton {
                objectName: "showContactMarks"
                text: qsTr("Show contact areas"); checked: ctrl.showContactMarks
                onClicked: ctrl.showContactMarks = !ctrl.showContactMarks
            }
            HelpChip { key: "pb-contact-marks-toggle"; anchors.verticalCenter: parent.verticalCenter }
        }
        Text {
            objectName: "contactMarksHint"
            textFormat: Text.PlainText
            visible: ctrl.contactMarks.hint !== undefined
            width: parent.width; wrapMode: Text.WordWrap
            text: ctrl.contactMarks.hint !== undefined ? ctrl.contactMarks.hint : ""
            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "videoPlayAutostart"
                text: qsTr("Video play starts device")
                checked: ctrl.videoPlayAutostart
                onClicked: ctrl.videoPlayAutostart = !ctrl.videoPlayAutostart
            }
            HelpChip { key: "pb-video-play-autostart"; anchors.verticalCenter: parent.verticalCenter }
        }
        InspectorNote { text: qsTr("When off, the film can play without moving the toy. Script Play (no video) still starts the device.") }
        Row {
            spacing: 8
            FlowButton {
                objectName: "pb-audio-vibration-view"
                text: qsTr("Audio vibration")
                checked: ctrl.audioVibration
                onClicked: ctrl.audioVibration = !ctrl.audioVibration
            }
            HelpChip { key: "pb-audio-vibration"; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "pb-mock"
                text: qsTr("Mock (no device)")
                checked: ctrl.playbackMock
                onClicked: ctrl.playbackMock = !ctrl.playbackMock
            }
            HelpChip { key: "pb-mock"; anchors.verticalCenter: parent.verticalCenter }
        }
        InspectorNote {
            text: qsTr("On (default): Play connects a simulated device when nothing is connected. Off: connect a real toy in Devices first.")
        }
        // MT-Debug tip/partner path (0.7 Show tip path).
        Row {
            spacing: 8
            FlowButton {
                objectName: "showTrajectory"
                visible: ctrl.hasTrajectory
                text: qsTr("Show tip path"); checked: ctrl.trajectoryOverlay
                onClicked: ctrl.trajectoryOverlay = !ctrl.trajectoryOverlay
            }
            HelpChip {
                visible: ctrl.hasTrajectory
                anchors.verticalCenter: parent.verticalCenter
                key: "pb-trajectory-toggle"
            }
        }
        Text {
            objectName: "trajectoryHint"
            textFormat: Text.PlainText
            visible: ctrl.hasScript && !ctrl.hasTrajectory
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("This script has no recorded trajectory — regenerate with contact marks (Record tip path).")
            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
        }
        // Speed highlights (0.7 pb-speed-hl): red bands over too fast strokes.
        Row {
            spacing: 10
            FlowButton {
                text: qsTr("Speed highlights"); checked: ctrl.speedHighlights
                onClicked: ctrl.speedHighlights = !ctrl.speedHighlights
                HoverHandler { id: hlHover }
                ToolTipText { shown: hlHover.hovered; text: qsTr("Paint red “too fast” bands on the curve (OFS community intensity). Display only — separate from Speed-cap edit.") }
            }
            HelpChip { key: "pb-speed-hl"; anchors.verticalCenter: parent.verticalCenter }
            FlowSlider {
                anchors.verticalCenter: parent.verticalCenter; width: 76
                enabled: ctrl.speedHighlights
                from: 150; to: 800; value: ctrl.speedHighlightThresh
                onMoved: (v) => ctrl.speedHighlightThresh = Math.round(v / 25) * 25
                HoverHandler { id: hlThHover }
                ToolTipText { shown: hlThHover.hovered; text: qsTr("Intensity threshold for Speed highlights on the curve (500×|Δpos|/|Δt|). Lower = more red bands.") }
            }
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: Math.round(ctrl.speedHighlightThresh); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
            HelpChip { key: "playback.hl"; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 8
            width: parent.width
            visible: ctrl.hasScript
            Text {
                textFormat: Text.PlainText
                width: parent.width - 26
                wrapMode: Text.WordWrap
                text: qsTr("Heatmap: click = seek, drag = mark. Wheel on the heatmap zooms. Curve edit below uses the marked range.")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            HelpChip { objectName: "heatmapHintHelp"; key: "pb-marker-hint"; anchors.top: parent.top }
        }
    }
    // Curve edit on the heatmap selection (0.7 Cap · Scale · Delete · Invert).
    MessageDialog {
        id: invertAllDialog
        objectName: "curveInvertAllDialog"
        title: qsTr("Invert the curve?")
        text: qsTr("Invert the whole lane? (Undo can bring it back.)")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: ctrl.invertAll()
    }
    InspectorCard {
        id: curveEdit
        objectName: "curveEditCard"
        title: qsTr("Curve edit")
        help: "playback.cap"
        Text {
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Rewrite the picked lane inside the marked heatmap range. Stroke always; vibration/suction when the script carries those lanes. Invert without a mark flips the whole lane.")
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        Row {
            spacing: 8
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                text: qsTr("Cap")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            FlowSlider {
                objectName: "curveCapSlider"
                anchors.verticalCenter: parent.verticalCenter
                width: curveEdit.width - 24 - 78 - 40
                from: 150; to: 800; value: ctrl.capIntensity
                onMoved: (v) => ctrl.capIntensity = Math.round(v / 25) * 25
            }
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: Math.round(ctrl.capIntensity)
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
            }
        }
        FlowButton {
            objectName: "curveCap"
            text: qsTr("Speed-cap selection")
            enabled: ctrl.hasScript && ctrl.hasMarker
            onClicked: ctrl.capSelection()
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "curveSoftEdges"
                text: qsTr("Soft edges"); checked: ctrl.scaleSoftEdges
                onClicked: ctrl.scaleSoftEdges = !ctrl.scaleSoftEdges
            }
            HelpChip { key: "pb-scale-soft-edges"; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "curveSofter"
                text: qsTr("Softer")
                enabled: ctrl.hasScript && ctrl.hasMarker
                onClicked: ctrl.scaleSelection(0.8)
            }
            FlowButton {
                objectName: "curveStronger"
                text: qsTr("Stronger")
                enabled: ctrl.hasScript && ctrl.hasMarker
                onClicked: ctrl.scaleSelection(1.25)
            }
            HelpChip { key: "playback.scale"; anchors.verticalCenter: parent.verticalCenter }
            FlowButton {
                objectName: "curveDelete"
                text: qsTr("Delete range")
                enabled: ctrl.hasScript && ctrl.hasMarker
                onClicked: ctrl.deleteSelection()
            }
            FlowButton {
                objectName: "curveInvert"
                text: ctrl.hasMarker ? qsTr("Invert range") : qsTr("Invert all")
                enabled: ctrl.hasScript
                onClicked: {
                    if (ctrl.hasMarker) ctrl.invertSelection()
                    else invertAllDialog.open()
                }
            }
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "curveUndo"
                text: qsTr("Undo")
                enabled: ctrl.canUndo
                onClicked: ctrl.undo()
                HoverHandler { id: curveUndoHover }
                ToolTipText {
                    shown: curveUndoHover.hovered
                    text: ctrl.canUndo ? ctrl.undoText + qsTr(" (Ctrl+Z)") : ctrl.undoText
                }
            }
            FlowButton {
                objectName: "curveRedo"
                text: qsTr("Redo")
                enabled: ctrl.canRedo
                onClicked: ctrl.redo()
                HoverHandler { id: curveRedoHover }
                ToolTipText {
                    shown: curveRedoHover.hovered
                    text: ctrl.canRedo ? ctrl.redoText + qsTr(" (Ctrl+Y)") : ctrl.redoText
                }
            }
        }
        Row {
            spacing: 8
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                text: qsTr("FPS-Snap")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            FlowSlider {
                objectName: "fpsSnap"
                anchors.verticalCenter: parent.verticalCenter
                width: curveEdit.width - 24 - 78 - 48 - 26
                from: 0; to: 60; value: ctrl.fpsSnap
                onMoved: (v) => ctrl.fpsSnap = Math.round(v)
            }
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                text: ctrl.fpsSnap > 0 ? qsTr("%1").arg(Math.round(ctrl.fpsSnap)) : qsTr("off")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            HelpChip { key: "playback.fps-snap"; anchors.verticalCenter: parent.verticalCenter }
        }
        InspectorNote { text: qsTr("Seeks snap to frame boundaries when FPS-Snap is on (0 = off).") }
        InspectorNote { visible: !ctrl.hasMarker; text: qsTr("Mark a range for Cap / Scale / Delete. Invert all works without one.") }
        InspectorNote { objectName: "curveEditNote"; visible: ctrl.toolNote !== ""; text: ctrl.toolNote }
        FlowButton {
            objectName: "editScriptFromCurve"
            text: qsTr("More editing in the Editor…"); icon: "edit"
            enabled: ctrl.hasScript
            onClicked: rootPanel.editRequested(false)
        }
    }
    // Project & heatmap export (0.7 Tools "Project & heatmap export").
    InspectorCard {
        title: qsTr("Project & heatmap export")
        Flow {
            width: parent.width
            spacing: 8
            FlowButton {
                text: qsTr("Heatmap PNG"); enabled: ctrl.hasScript
                HoverHandler { id: hmHover }
                ToolTipText { shown: hmHover.hovered; text: qsTr("Intensity heatmap as PNG (chapters as ticks)") }
                onClicked: {
                    const path = ctrl.exportHeatmapPng()
                    rootPanel.projectStatus = path !== "" ? qsTr("Heatmap: %1").arg(path) : ctrl.lastError
                }
            }
            FlowButton {
                text: qsTr("Save project"); enabled: ctrl.hasScript
                HoverHandler { id: spHover }
                ToolTipText { shown: spHover.hovered; text: qsTr("Save video+script+offset as .snp.json") }
                onClicked: {
                    const path = ctrl.saveProject(Math.round(rootPanel.videoPositionMs))
                    rootPanel.projectStatus = path !== "" ? qsTr("Project: %1").arg(path) : ctrl.lastError
                }
            }
            FlowButton {
                text: qsTr("Load project")
                HoverHandler { id: lpHover }
                ToolTipText { shown: lpHover.hovered; text: qsTr("Open a .snp.json project (script, video, offset, seek, loop)") }
                onClicked: rootPanel.projectLoadRequested()
            }
        }
        InspectorNote { visible: rootPanel.projectStatus !== ""; text: rootPanel.projectStatus; wrapMode: Text.WrapAnywhere }
    }
    // O-markers (0.7 View): from the marked range on the heatmap.
    InspectorCard {
        title: qsTr("O-markers")
        help: "pb-omarker-hint"
        FlowCombo {
            id: oKind
            width: parent.width
            model: [qsTr("Primary (peak)"), qsTr("Secondary (earlier, softer)")]
        }
        Row {
            visible: oKind.currentIndex === 1
            spacing: 10
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Intensity"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            FlowSlider {
                anchors.verticalCenter: parent.verticalCenter; width: 130
                from: 0; to: 1; value: ctrl.oMarkerIntensity
                onMoved: (v) => ctrl.oMarkerIntensity = Math.round(v * 20) / 20
            }
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: Math.round(ctrl.oMarkerIntensity * 100) + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
            HelpChip { key: "playback.intensity"; anchors.verticalCenter: parent.verticalCenter }
        }
        FlowButton {
            text: qsTr("Apply as O-marker"); enabled: ctrl.hasMarker
            onClicked: ctrl.addOMarkerFromSelection(oKind.currentIndex === 1 ? "secondary" : "primary")
        }
        InspectorNote { visible: !ctrl.hasMarker; text: qsTr("Mark a range on the heatmap first.") }
        MarkList {
            width: parent.width
            height: Math.min(200, Math.max(30, count * 38))
            model: ctrl.oMarkers
            canRename: false
            emptyText: qsTr("No O-markers yet.")
            onSeek: (i) => rootPanel.seekRequested(ctrl.oMarkers[i].startMs + ctrl.offsetMs)
            onRemove: (i) => ctrl.removeOMarker(i)
        }
    }
    InspectorCard {
        title: qsTr("Full curve editing")
        help: "pb-curve-edit-hint"
        InspectorNote { text: qsTr("Point drag, undo and scenes are in the Generator (the Editor). Cap / Softer / Stronger / Delete / Invert on a heatmap mark are under Curve edit above.") }
        FlowButton { objectName: "editScript"; text: qsTr("Edit this script"); icon: "edit"; enabled: ctrl.hasScript; onClicked: rootPanel.editRequested(false) }
    }
}
