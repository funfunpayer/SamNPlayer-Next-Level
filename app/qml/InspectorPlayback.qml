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
        id: adapt
        objectName: "adaptCard"
        title: qsTr("Adapt for device")
        help: "pb-optimize-target"
        readonly property var targets: ["general", "neo2"]
        readonly property int preferredTarget: (device && device.connected && device.kind === "samneo2") ? 1 : 0
        Text {
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Rewrite a finished script so it feels right on the toy — heal gaps, or bake Sam Neo 2 vibration and suction.")
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        Row {
            spacing: 8
            FlowCombo {
                id: adaptTarget
                objectName: "adaptTarget"
                width: adapt.width - 24 - 118
                model: [qsTr("Any device"), qsTr("Sam Neo 2")]
                currentIndex: adapt.preferredTarget
            }
            FlowButton {
                objectName: "adaptOptimize"
                text: qsTr("Optimize")
                icon: "sliders"
                enabled: ctrl.hasScript
                onClicked: adaptConfirm.open()
            }
        }
        FlowButton {
            objectName: "adaptEditTools"
            text: qsTr("More tools in the Editor…")
            icon: "edit"
            enabled: ctrl.hasScript
            onClicked: rootPanel.editRequested(false)
        }
        InspectorNote { text: qsTr("Optimize saves into the script (and a .samn Emotion Script for Neo 2). Bake & share below writes feel channels or a community .funscript. Effects stay live playback feel only.") }
        InspectorNote { objectName: "adaptNote"; visible: ctrl.toolNote !== ""; text: ctrl.toolNote }
        MessageDialog {
            id: adaptConfirm
            title: qsTr("Optimize & save")
            text: adaptTarget.currentIndex === 1
                  ? qsTr("Heal the stroke, add contact vibration and bake Neo 2 vibration / suction into the Emotion Script beside this file?")
                  : qsTr("Fill and heal stroke gaps and save the script so it plays cleanly on any device?")
            buttons: MessageDialog.Ok | MessageDialog.Cancel
            onAccepted: ctrl.optimizeFor(adapt.targets[adaptTarget.currentIndex])
        }
    }
    // 0.7 Tools "Bake & share": bake Neo feel channels + community export.
    InspectorCard {
        id: bakeShare
        objectName: "bakeShareCard"
        title: qsTr("Bake & share")
        help: "pb-bake-share"
        Text {
            textFormat: Text.PlainText
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Bake writes Neo vibration and suction into the Emotion Script. Share exports a community .funscript other apps can open.")
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        FlowButton {
            objectName: "bakeFeelChannels"
            text: qsTr("Bake feel channels")
            icon: "effects"
            enabled: ctrl.hasScript
            onClicked: bakeFeelConfirm.open()
        }
        FlowButton {
            objectName: "shareForApps"
            text: qsTr("Share for other apps")
            icon: "download"
            enabled: ctrl.hasScript
            onClicked: {
                const path = ctrl.exportFunscript()
                if (path === "") rootPanel.projectStatus = ctrl.lastError
            }
        }
        InspectorNote { objectName: "bakeShareNote"; visible: ctrl.toolNote !== ""; text: ctrl.toolNote }
        MessageDialog {
            id: bakeFeelConfirm
            objectName: "bakeFeelConfirm"
            title: qsTr("Bake feel channels")
            text: qsTr("Write vibration and suction lanes from the stroke into the Emotion Script (.samn) beside this file?")
            buttons: MessageDialog.Ok | MessageDialog.Cancel
            onAccepted: ctrl.axesFromStroke()
        }
    }
    InspectorCard {
        id: scriptTools
        title: qsTr("Script Tools")
        help: "playback.script-offset"
        Row {
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Script offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            OffsetField { value: ctrl.offsetMs; onChanged: (v) => ctrl.offsetMs = v }
        }
        Row {
            spacing: 8
            FlowButton { text: qsTr("Reset"); enabled: ctrl.offsetMs !== 0; onClicked: ctrl.offsetMs = 0 }
            FlowButton {
                objectName: "bakeOffset"
                text: qsTr("Bake offset")
                enabled: ctrl.offsetMs !== 0 && ctrl.scriptPath !== ""
                onClicked: bakeConfirm.open()
            }
            HelpChip { key: "pb-offset-bake"; anchors.verticalCenter: parent.verticalCenter }
            // 0.7 asked first: this rewrites the script file.
            MessageDialog {
                id: bakeConfirm
                title: qsTr("Bake offset")
                text: qsTr("Move every point, bookmark and chapter by %1 ms and save the script? The offset then goes back to 0.").arg(ctrl.offsetMs)
                buttons: MessageDialog.Ok | MessageDialog.Cancel
                onAccepted: ctrl.bakeOffset()
            }
        }
        InspectorNote { text: qsTr("Positive: the script comes later. Type a value (step 10 ms, ±10 000) or use ±50. Bake writes it into the script's times; the offset goes back to 0.") }
        Row {
            spacing: 8
            FlowButton {
                objectName: "autoSyncSuggest"
                text: ctrl.autoSyncBusy ? qsTr("Listening…") : qsTr("Auto sync")
                enabled: ctrl.hasScript && ctrl.videoName !== "" && !ctrl.autoSyncBusy
                onClicked: ctrl.suggestAudioSync()
            }
            HelpChip { key: "pb-auto-sync"; anchors.verticalCenter: parent.verticalCenter }
            FlowButton {
                objectName: "autoSyncApply"
                text: ctrl.autoSyncConfidence !== "none"
                      ? qsTr("Apply %1 ms").arg(ctrl.autoSyncOffsetMs >= 0
                            ? ("+" + ctrl.autoSyncOffsetMs)
                            : String(ctrl.autoSyncOffsetMs))
                      : qsTr("Apply")
                enabled: ctrl.autoSyncCanApply
                onClicked: ctrl.applyAudioSyncSuggestion()
            }
        }
        InspectorNote {
            objectName: "autoSyncNote"
            visible: ctrl.autoSyncNote !== ""
            text: ctrl.autoSyncNote
        }
        InspectorNote { text: qsTr("Auto sync compares the video's sound (first 10 min) with the stroke and suggests an offset. Apply only changes the offset here; Bake writes it into the script.") }
        Row {
            spacing: 8
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                text: qsTr("Curve")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            FlowCombo {
                objectName: "curveSelect"
                width: scriptTools.width - 24 - 86
                enabled: ctrl.hasScript && ctrl.laneChoices.length > 0
                model: ctrl.laneChoices.map((l) => rootPanel.laneTitle(l))
                currentIndex: Math.max(0, ctrl.laneChoices.indexOf(ctrl.activeLane))
                onActivated: (i) => ctrl.activeLane = ctrl.laneChoices[i]
            }
        }
        Row {
            spacing: 8
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                text: qsTr("+ Axis…")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            FlowCombo {
                objectName: "plusAxis"
                width: scriptTools.width - 24 - 86
                enabled: ctrl.hasScript && ctrl.missingAxisChoices.length > 0
                model: [qsTr("Choose axis…")].concat(ctrl.missingAxisChoices.map((a) => rootPanel.axisTitle(a)))
                currentIndex: 0
                onActivated: (i) => {
                    if (i <= 0) return
                    const axis = ctrl.missingAxisChoices[i - 1]
                    const path = ctrl.createCompanionAxis(axis)
                    currentIndex = 0
                    if (path === "") rootPanel.projectStatus = ctrl.lastError
                }
            }
        }
        Row {
            spacing: 8
            Text {
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: 78
                text: qsTr("Drive")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            FlowCombo {
                objectName: "driveSource"
                width: scriptTools.width - 24 - 86
                enabled: ctrl.hasScript
                model: [qsTr("Recipe"), qsTr("Axes")]
                currentIndex: ctrl.playbackSource === "axes" ? 1 : 0
                onActivated: (i) => ctrl.setPlaybackSource(i === 1 ? "axes" : "recipe")
            }
        }
        FlowButton {
            objectName: "saveEmotionScript"
            text: qsTr("Save Emotion Script")
            icon: "save"
            enabled: ctrl.hasScript
            onClicked: {
                const path = ctrl.saveAsEmotionScript()
                if (path === "") rootPanel.projectStatus = ctrl.lastError
            }
        }
        InspectorNote { text: qsTr("Curve picks the lane the main curve shows. + Axis… creates a centred companion file (surge/sway/twist/roll/pitch) beside the script. Drive Recipe/Axes and Save Emotion Script write the .samn.") }
        InspectorNote { objectName: "bakeNote"; visible: ctrl.toolNote !== ""; text: ctrl.toolNote }
    }
    // Effects (0.7 Player Inspector → Effects and "Playback &
    // Extended-O"): the presets, Intensity and the mapping
    // settings. A change maps the script again at once.
    InspectorCard {
        id: fx
        title: qsTr("Effects")
        readonly property var mapping: ctrl.mapping
        readonly property var names: mapping.effectPresets
        readonly property bool savedSelected: names.indexOf(mapping.effectPreset) >= 0
        Row {
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Preset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            FlowCombo {
                objectName: "fxPreset"
                width: fx.width - 24 - 86
                model: [qsTr("Custom"), qsTr("Default")].concat(fx.names)
                currentIndex: fx.mapping.effectPreset === "" ? 0
                            : fx.mapping.effectPreset === "__default" ? 1
                            : 2 + fx.names.indexOf(fx.mapping.effectPreset)
                onActivated: (i) => fx.mapping.applyEffectPreset(i === 0 ? "" : i === 1 ? "__default" : fx.names[i - 2])
                HoverHandler { id: fxHover }
                ToolTipText { shown: fxHover.hovered; text: qsTr("Saved effect settings: Intensity, Smoothing, Max speed, Tick, Soft start and Reduce peaks. Default brings back the standard values.") }
            }
        }
        Row {
            spacing: 8
            FlowTextField {
                id: fxName
                objectName: "fxName"
                width: fx.width - 24 - fxSave.width - fxDelete.width - 16; implicitHeight: 34
                placeholder: qsTr("Preset name")
                maxLength: 40
                onAccepted: fxSave.clicked()
            }
            FlowButton {
                id: fxSave
                objectName: "fxSave"
                text: qsTr("Save"); implicitHeight: 34
                enabled: fxName.text.trim() !== "" && fxName.text.trim() !== "__default"
                onClicked: if (enabled && fx.mapping.saveEffectPreset(fxName.text)) fxName.text = ""
                HoverHandler { id: fxSaveHover }
                ToolTipText { shown: fxSaveHover.hovered; text: qsTr("Save the current effect settings under this name (the same name replaces it).") }
            }
            FlowButton {
                id: fxDelete
                objectName: "fxDelete"
                text: qsTr("Delete"); implicitHeight: 34
                enabled: fx.savedSelected
                onClicked: fx.mapping.deleteEffectPreset(fx.mapping.effectPreset)
                HoverHandler { id: fxDeleteHover }
                ToolTipText { shown: fxDeleteHover.hovered; text: qsTr("Delete the selected preset") }
            }
        }
        InspectorSliderRow {
            label: qsTr("Intensity")
            tip: qsTr("Overall output of vibration and suction, on top of the channel levels. Live.")
            from: 0; to: 100; value: ctrl.output.intensity
            valueText: ctrl.output.intensity + " %"
            onPicked: (v) => ctrl.output.intensity = Math.round(v)
        }
        InspectorSliderRow {
            objectName: "fxSmoothing"
            label: qsTr("Smoothing")
            help: "playback.smoothing"
            tip: qsTr("Live curve smoothing toward the next sample (0–1). Default 0.3. Playback feel only.")
            from: 0; to: 1; value: fx.mapping.smoothing
            valueText: fx.mapping.smoothing.toFixed(2)
            onPicked: (v) => fx.mapping.smoothing = Math.round(v * 20) / 20
        }
        InspectorSliderRow {
            label: qsTr("Max speed")
            help: "playback.max-speed"
            tip: qsTr("The stroke speed (position change per millisecond, 0–100 scale) that gives full strength. Default 0.6. Playback feel only.")
            from: 0.1; to: 2; value: fx.mapping.maxSpeed
            valueText: fx.mapping.maxSpeed.toFixed(1)
            onPicked: (v) => fx.mapping.maxSpeed = Math.round(v * 10) / 10
        }
        InspectorSliderRow {
            label: qsTr("Tick")
            help: "playback.tick-ms"
            tip: qsTr("Device command interval. Lower = denser stream. Default 50 ms. Playback feel only.")
            from: 10; to: 200; value: fx.mapping.tickMs
            valueText: qsTr("%1 ms").arg(fx.mapping.tickMs)
            onPicked: (v) => fx.mapping.tickMs = Math.round(v / 10) * 10
        }
        Row {
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Sync mode"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                   HoverHandler { id: syncHover }
                   ToolTipText { shown: syncHover.hovered; text: qsTr("How vibration and suction follow the stroke. Applies to stroke scripts; a Tf / Tj script keeps its own unless you choose another than Independent; scripts with their own vibration and suction curves keep theirs.") } }
            FlowCombo {
                objectName: "syncMode"
                width: fx.width - 24 - 86
                model: [qsTr("Independent"), qsTr("Synchronized"), qsTr("Alternating"), qsTr("Vibration only"),
                        qsTr("Suction only"), qsTr("Suction from position")]
                currentIndex: Math.max(0, fx.mapping.syncModes.indexOf(fx.mapping.syncMode))
                onActivated: (i) => fx.mapping.syncMode = fx.mapping.syncModes[i]
            }
        }
        InspectorNote { text: qsTr("Soft start and Reduce peaks are under Devices → Output; presets keep them too. The lanes and the device follow at once; the script file is not changed.") }
    }
    // Contact vibration and the strength presets (0.7 Player
    // "Contact vibration" block and the Strength choice).
    InspectorCard {
        id: contact
        title: qsTr("Contact vibration & strength")
        visible: (ctrl.contactControls && ctrl.playbackSource !== "axes") || ctrl.strengthPresets.length > 0
        readonly property var mapping: ctrl.mapping
        readonly property var probe: ctrl.contactFeelProbe
        readonly property bool contactLive: ctrl.contactControls && ctrl.playbackSource !== "axes"
        Row {
            spacing: 8
            visible: contact.contactLive
            FlowButton {
                objectName: "contactOff"
                text: qsTr("Contact vibration off"); checked: contact.mapping.contactOff
                onClicked: contact.mapping.contactOff = !contact.mapping.contactOff
                HoverHandler { id: offHover }
                ToolTipText { shown: offHover.hovered; text: qsTr("Turns off the contact vibration the script carries, without changing the file.") }
            }
            HelpChip { key: "pb-contact-off"; anchors.verticalCenter: parent.verticalCenter }
        }
        InspectorSliderRow {
            visible: contact.contactLive
            label: qsTr("Strength")
            help: "playback.contact-strength"
            tip: qsTr("Live scaling of the contact vibration without rewriting the file. 1 = as created, 0 = off, up to 2 = stronger.")
            from: 0; to: 2; value: contact.mapping.contactIntensity
            valueText: contact.mapping.contactIntensity === 0 ? qsTr("0 (off)") : contact.mapping.contactIntensity.toFixed(2)
            onPicked: (v) => contact.mapping.contactIntensity = Math.round(v * 20) / 20
        }
        InspectorSliderRow {
            visible: contact.contactLive
            active: ctrl.contactKnobsLive
            label: qsTr("Sensitivity")
            help: "playback.sensitivity"
            tip: qsTr("Lower = the vibration starts earlier in the stroke. Starts from the script's recipe; Save to script keeps it.")
            from: 0.4; to: 0.95; value: contact.mapping.contactSpan
            valueText: contact.mapping.contactSpan.toFixed(2)
            onPicked: (v) => contact.mapping.contactSpan = Math.round(v * 20) / 20
        }
        Row {
            visible: contact.contactLive
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Curve"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            FlowCombo {
                objectName: "contactCurve"
                width: contact.width - 24 - 86
                enabled: ctrl.contactKnobsLive; opacity: enabled ? 1 : 0.45
                model: [qsTr("Linear"), qsTr("Soft (contact-like)"), qsTr("Peak"), qsTr("Impulse (peaks only)")]
                currentIndex: Math.max(0, contact.mapping.contactCurves.indexOf(contact.mapping.contactCurve))
                onActivated: (i) => contact.mapping.contactCurve = contact.mapping.contactCurves[i]
            }
        }
        // 0.7 Contact feel probe: synthetic bounce through Sensitivity / Curve / Strength.
        Column {
            objectName: "contactFeelProbe"
            visible: contact.contactLive
            width: parent.width
            spacing: 4
            Row {
                spacing: 8
                width: parent.width
                Text {
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Feel probe")
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                HelpChip { key: "gen-feel-probe"; anchors.verticalCenter: parent.verticalCenter }
                Item { width: Math.max(0, parent.width - 200); height: 1 }
                Text {
                    objectName: "playerFeelProbeActive"
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1% active").arg(Math.round(contact.probe.activePct || 0))
                    color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                Text {
                    objectName: "playerFeelProbePeak"
                    textFormat: Text.PlainText
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("peak %1").arg(Math.round(contact.probe.peak || 0))
                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                }
            }
            Canvas {
                id: playerFeelProbeCanvas
                objectName: "playerFeelProbeCanvas"
                width: parent.width
                height: 72
                property var probe: contact.probe
                onProbeChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const w = width, h = height
                    ctx.fillStyle = Qt.rgba(1, 1, 1, 0.04)
                    ctx.fillRect(0, 0, w, h)
                    const stroke = probe.stroke || []
                    const vib = probe.vib || []
                    const peaks = probe.peaks || []
                    function poly(pts, color, width) {
                        if (pts.length < 2) return
                        ctx.beginPath()
                        ctx.strokeStyle = color
                        ctx.lineWidth = width
                        ctx.lineJoin = "round"
                        for (let i = 0; i < pts.length; ++i) {
                            const x = pts[i].x * w
                            const y = (1 - pts[i].y) * h
                            if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                        }
                        ctx.stroke()
                    }
                    poly(stroke, Theme.orange, 1.5)
                    poly(vib, Theme.cyan, 1.75)
                    ctx.fillStyle = Theme.cyan
                    for (let i = 0; i < peaks.length; ++i) {
                        const x = peaks[i].x * w
                        const y = (1 - peaks[i].y) * h
                        ctx.beginPath()
                        ctx.arc(x, y, 2.5, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
            Text {
                objectName: "playerFeelProbeHint"
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                text: contact.probe.hint || ""
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11
            }
        }
        FlowButton {
            objectName: "saveContactToScript"
            visible: contact.contactLive
            text: qsTr("Save to script")
            icon: "save"
            enabled: ctrl.hasScript && ctrl.contactKnobsLive
            onClicked: ctrl.saveContactToScript()
            HoverHandler { id: saveContactHover }
            ToolTipText { shown: saveContactHover.hovered; text: qsTr("Write Contact on/off, Sensitivity and Curve into the Emotion Script recipe.") }
        }
        InspectorNote {
            objectName: "contactSaveNote"
            visible: ctrl.toolNote !== "" && ctrl.toolNote.indexOf("Contact settings") === 0
            text: ctrl.toolNote
        }
        InspectorNote {
            visible: contact.contactLive && !ctrl.contactKnobsLive
            text: qsTr("Sensitivity and curve act when the script plays from its recipe with contact vibration on.")
        }
        Row {
            visible: ctrl.strengthPresets.length > 0
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Preset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                   HoverHandler { id: strengthHover }
                   ToolTipText { shown: strengthHover.hovered; text: qsTr("Soft / normal / strong feel of this Emotion Script. Saved in the script.") } }
            FlowCombo {
                objectName: "strengthPreset"
                width: contact.width - 24 - 86
                model: ["—"].concat(ctrl.strengthPresets)
                currentIndex: ctrl.activeStrength === "" ? 0 : 1 + ctrl.strengthPresets.indexOf(ctrl.activeStrength)
                onActivated: (i) => ctrl.activeStrength = i === 0 ? "" : ctrl.strengthPresets[i - 1]
            }
        }
    }
    // Extended-O (0.7 "Playback & Extended-O" and the transport's
    // Extended-O button; key E).
    InspectorCard {
        title: qsTr("Extended-O")
        Flow {
            width: parent.width; spacing: 8
            FlowButton {
                objectName: "eoTrigger"
                text: ctrl.output.extendedOActive ? qsTr("Running…") : qsTr("Extended-O")
                primary: true
                enabled: ctrl.output.canTriggerExtendedO && !ctrl.output.extendedOActive
                onClicked: ctrl.output.triggerExtendedO()
                HoverHandler { id: eoHover }
                ToolTipText { shown: eoHover.hovered; text: qsTr("Lowers vibration and suction to the minimum for the hold time, then brings them back. The curve keeps its rhythm. While playing (E).") }
            }
            FlowButton {
                text: qsTr("Enabled"); checked: ctrl.output.extendedOEnabled
                onClicked: ctrl.output.extendedOEnabled = !ctrl.output.extendedOEnabled
            }
        }
        InspectorSliderRow {
            label: qsTr("Minimum")
            tip: qsTr("Height of the curve while held, as a share of the script (0 % = off). Default 10 %.")
            from: 0; to: 1; value: ctrl.output.extendedOMin
            valueText: ctrl.output.extendedOMin === 0 ? qsTr("0 % (off)") : Math.round(ctrl.output.extendedOMin * 100) + " %"
            onPicked: (v) => ctrl.output.extendedOMin = Math.round(v * 20) / 20
        }
        InspectorSliderRow {
            label: qsTr("Hold")
            help: "playback.hold-s"
            tip: qsTr("How long the minimum is held. Default 10 s.")
            from: 1; to: 60; value: ctrl.output.extendedOHoldS
            valueText: qsTr("%1 s").arg(Math.round(ctrl.output.extendedOHoldS))
            onPicked: (v) => ctrl.output.extendedOHoldS = Math.round(v)
        }
        InspectorSliderRow {
            label: qsTr("Restore")
            help: "playback.restore-ms"
            tip: qsTr("Ramp time down to the minimum and back up again. 0 switches at once. Default 500 ms.")
            from: 0; to: 3000; value: ctrl.output.extendedORestoreMs
            valueText: ctrl.output.extendedORestoreMs === 0 ? qsTr("At once") : qsTr("%1 ms").arg(ctrl.output.extendedORestoreMs)
            onPicked: (v) => ctrl.output.extendedORestoreMs = Math.round(v / 100) * 100
        }
        FlowButton {
            visible: ctrl.hasScript
            text: qsTr("Auto in marked range"); checked: ctrl.autoExtendedO
            onClicked: ctrl.autoExtendedO = !ctrl.autoExtendedO
            HoverHandler { id: autoEoHover }
            ToolTipText { shown: autoEoHover.hovered; text: qsTr("Triggers Extended-O once when playback reaches the marked range of the heatmap.") }
        }
        InspectorNote {
            visible: ctrl.hasScript
            text: !ctrl.output.extendedOEnabled ? qsTr("Switched off.")
                  : ctrl.autoExtendedO && !ctrl.hasMarker ? qsTr("Mark a range on the heatmap for the automatic trigger.")
                  : qsTr("Available while playing. A press while it runs is ignored.")
        }
    }
    // Script Doctor (0.7 pb-quality): signal quality from the script alone.
    InspectorCard {
        id: doctor
        title: qsTr("Script Doctor")
        property var result: null
        FlowButton {
            text: qsTr("Check script"); enabled: ctrl.hasScript
            onClicked: doctor.result = ctrl.scriptQuality()
        }
        Rectangle {
            visible: doctor.result !== null
            width: parent.width; radius: Theme.radiusSmall
            height: doctorText.implicitHeight + 16
            readonly property bool failed: doctor.result !== null && (doctor.result.error !== "" || !doctor.result.passed)
            color: failed ? Qt.rgba(Theme.danger.r, Theme.danger.g, Theme.danger.b, 0.12)
                          : Qt.rgba(Theme.good.r, Theme.good.g, Theme.good.b, 0.12)
            border.color: failed ? Theme.danger : Theme.good
            Text {
                id: doctorText
                x: 8; y: 8; width: parent.width - 16
                wrapMode: Text.Wrap; textFormat: Text.StyledText
                color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                text: {
                    const r = doctor.result
                    if (r === null) return ""
                    if (r.error !== "") return qsTr("Quality check: %1").arg(r.error)
                    let t = "<b>" + qsTr("Signal Quality (Script Doctor): %1% %2").arg(Math.round(r.score * 100))
                            .arg(r.passed ? qsTr("(within normal)") : qsTr("(review recommended)")) + "</b><br>"
                    t += "<font color='#9fb3c8'>" + qsTr("Signal quality only — jitter, jumps, gaps, device density. Not proof the curve matches the video (that would be motion fidelity / phase comparison). Estimated from the script only, without video; tracking-based checks are missing here, unlike right after generation.") + "</font>"
                    for (const w of r.warnings) t += "<br>• " + w
                    return t
                }
            }
        }
        // A new script: the old verdict no longer applies.
        Connections { target: ctrl; function onScriptChanged() { doctor.result = null } }
    }
    // Play list (0.7 pb-playlist): shown with 2+ scripts.
    InspectorCard {
        title: qsTr("Playlist")
        Flow {
            width: parent.width; spacing: 8
            FlowButton {
                text: qsTr("Add to list"); icon: "plus"; enabled: ctrl.hasScript
                HoverHandler { id: addHover }
                ToolTipText { shown: addHover.hovered; text: qsTr("Add another Emotion Script to the list") }
                onClicked: rootPanel.addToListRequested()
            }
            FlowButton { text: qsTr("Auto-advance"); checked: ctrl.playlistAuto; onClicked: ctrl.playlistAuto = !ctrl.playlistAuto }
            FlowButton { text: qsTr("Shuffle"); icon: "shuffle"; checked: ctrl.playlistShuffle; onClicked: ctrl.playlistShuffle = !ctrl.playlistShuffle }
            FlowButton { text: qsTr("Repeat playlist"); icon: "repeat"; checked: ctrl.playlistRepeat; onClicked: ctrl.playlistRepeat = !ctrl.playlistRepeat }
        }
        InspectorNote { visible: !ctrl.playlistVisible; text: qsTr("Drop several scripts or add one to play them in a row.") }
        Column {
            visible: ctrl.playlistVisible
            width: parent.width; spacing: 4
            Repeater {
                model: ctrl.playlist
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: parent.width; height: 32; radius: 8
                    color: modelData.active ? Qt.rgba(0, 120 / 255, 1, 0.22) : (rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                    border.width: modelData.active ? 1 : 0; border.color: Theme.cyan
                    Text {
                        textFormat: Text.PlainText
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        text: index + 1; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Text {
                        textFormat: Text.PlainText
                        x: 32; width: parent.width - 32 - 30; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name; elide: Text.ElideRight
                        color: modelData.active ? "white" : Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent; anchors.rightMargin: 30
                        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: ctrl.pickPlaylist(index)
                    }
                    IconButton {
                        anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter
                        width: 26; height: 26; icon: "close"; tip: qsTr("Remove")
                        onClicked: ctrl.removeFromPlaylist(index)
                    }
                }
            }
        }
    }
}
