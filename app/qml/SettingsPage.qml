import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SamN

// Settings behind the gear (D-146; 0.7 Tools → Settings / Log / About): a
// page over the current mode, Esc or × goes back to it.
Item {
    id: root
    property bool open: false
    // The Player (script folders, reload after "Honor inverted").
    property PlayerController player: null
    // The app's one device connection (Main.qml).
    property DeviceController device: null
    // The app's update check (Main.qml; D-147).
    property UpdateChecker updates: null
    property int tab: 0
    readonly property alias settings: ctl
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }

    SettingsController { id: ctl }
    FolderDialog {
        id: folderDialog
        title: qsTr("Add a script folder")
        onAccepted: if (root.player) root.player.addScriptFolder(selectedFolder.toString())
    }

    // Dim the app behind; a click outside the card closes.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1 / 255, 7 / 255, 15 / 255, 0.78)
        MouseArea { anchors.fill: parent; onClicked: root.open = false }
    }
    // Opaque under the glass: the mode behind must not show through the text.
    Rectangle {
        anchors.fill: card; scale: card.scale
        radius: Theme.radius; color: Theme.bgBottom
    }
    GlassCard {
        id: card
        objectName: "settingsCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 980); height: Math.min(parent.height - 48, 720)
        scale: root.open ? 1 : 0.97
        Behavior on scale { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }  // clicks on the card stay on it
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 14
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Icon { name: "settings"; color: Theme.cyan; width: 22; height: 22 }
                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Settings"); color: Theme.text; font.family: Theme.font; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Item { width: 12 }
                FlowTabs {
                    objectName: "settingsTabs"
                    model: [qsTr("General"), qsTr("Device"), qsTr("Log"), qsTr("About")]
                    current: root.tab
                    onPicked: (i) => root.tab = i
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    objectName: "settingsClose"
                    icon: "close"; tip: qsTr("Close (Esc)")
                    onClicked: root.open = false
                }
            }

            // ---- General ----
            Flickable {
                objectName: "generalTab"
                visible: root.tab === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                contentHeight: general.implicitHeight
                ColumnLayout {
                    id: general
                    width: parent.width; spacing: 14
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: playCol.implicitHeight + 28
                        ColumnLayout {
                            id: playCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Play"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("Honor inverted flag — a script marked \"inverted\" plays flipped (100 − position)")
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowToggle {
                                    objectName: "honorInverted"
                                    checked: ctl.honorInverted
                                    onToggled: (c) => {
                                        ctl.honorInverted = c
                                        // 0.7: changing it reloads the open script.
                                        if (root.player) root.player.reloadScript()
                                    }
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Script folders"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                            Repeater {
                                model: root.player ? root.player.scriptFolders : []
                                delegate: RowLayout {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Text {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true; elide: Text.ElideMiddle
                                        text: modelData; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                    FlowButton {
                                        objectName: "removeFolder_" + index
                                        text: qsTr("Remove")
                                        onClicked: root.player.removeScriptFolder(modelData)
                                    }
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                visible: !root.player || root.player.scriptFolders.length === 0
                                text: qsTr("No folders yet — only the video's own folder is searched.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowButton {
                                objectName: "addFolder"
                                text: qsTr("Add folder…")
                                enabled: root.player !== null
                                onClicked: folderDialog.open()
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: devCol.implicitHeight + 28
                        ColumnLayout {
                            id: devCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Device"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("On connect, run a short connection test (a brief vibration and suction pulse)")
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowToggle {
                                    objectName: "connectTest"
                                    checked: ctl.connectTest
                                    onToggled: (c) => ctl.connectTest = c
                                }
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: logCol.implicitHeight + 28
                        ColumnLayout {
                            id: logCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Logs and files"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                spacing: 10
                                Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("Log level"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowCombo {
                                    objectName: "logLevel"
                                    Layout.preferredWidth: 160
                                    model: ctl.logLevels
                                    currentIndex: Math.max(0, ctl.logLevels.indexOf(ctl.logLevel))
                                    onActivated: ctl.logLevel = currentText
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: qsTr("Log file: %1").arg(ctl.logFile !== "" ? ctl.logFile : qsTr("(no log file written yet)"))
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: qsTr("Settings and data: %1").arg(ctl.configFolder)
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                FlowButton { text: qsTr("Open log folder"); enabled: ctl.logFile !== ""; onClicked: ctl.openLogFolder() }
                                FlowButton { text: qsTr("Open settings folder"); onClicked: ctl.openConfigFolder() }
                            }
                        }
                    }
                    // 0.7 Runtime & updates, check only (D-147).
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: updCol.implicitHeight + 28
                        visible: root.updates !== null
                        ColumnLayout {
                            id: updCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Updates"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("Check for updates on startup")
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowToggle {
                                    objectName: "updateCheckOnStartup"
                                    checked: root.updates !== null && root.updates.checkOnStartup
                                    onToggled: (c) => root.updates.checkOnStartup = c
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: qsTr("Asks GitHub for the newest release; nothing is downloaded or installed by the app.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                FlowButton {
                                    objectName: "updateCheckNow"
                                    text: qsTr("Check for updates now")
                                    enabled: root.updates !== null && root.updates.state !== "checking"
                                    onClicked: root.updates.check()
                                }
                                FlowButton {
                                    objectName: "updateOpenPage"
                                    primary: true
                                    text: qsTr("Open download page")
                                    visible: root.updates !== null && root.updates.state === "available"
                                    onClicked: root.updates.openReleasePage()
                                }
                            }
                            Text {
                                objectName: "updateStatus"
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                visible: text !== ""
                                text: root.updates ? root.updates.statusText : ""
                                color: root.updates && root.updates.state === "error" ? Theme.danger
                                     : root.updates && root.updates.state === "available" ? Theme.cyan : Theme.textDim
                                font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                            Text {
                                objectName: "updateNotes"
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                visible: root.updates !== null && root.updates.state === "available" && root.updates.notesPreview !== ""
                                text: root.updates ? root.updates.notesPreview : ""
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            // ---- Device (0.7 Tools → Device: test, raw value, diagnostics) ----
            Flickable {
                id: devTab
                objectName: "deviceTab"
                visible: root.tab === 1
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                contentHeight: devCol2.implicitHeight
                readonly property var dev: root.device
                readonly property bool connected: dev !== null && dev.state === "connected"
                // 0.7: the test controls work only when connected and no playback / training runs.
                readonly property bool canTest: connected && !dev.sessionActive
                property var report: null
                property string diagStatus: ""
                // 0.7 levels: vibration 0-10, suction 0-5.
                function level(pct, steps) { return Math.round(pct / 100 * steps) }
                function showReport(json) {
                    try { report = JSON.parse(json) } catch (e) { report = null }
                    diagStatus = report ? qsTr("Done.") : ""
                }
                Connections {
                    target: root.device
                    function onDiagnosticsEntry(json) {
                        try {
                            const e = JSON.parse(json)
                            const v = typeof e.sentRaw === "number" ? e.sentRaw : e.wantedValue
                            devTab.diagStatus = qsTr("Running… %1").arg(e.phase.replace(/_/g, " "))
                                + (e.channel ? " · " + e.channel : "") + (v !== undefined ? " · " + v : "")
                                + (e.error ? " · " + qsTr("error: %1").arg(e.error) : "")
                        } catch (err) {}
                    }
                    function onDiagnosticsFinished(json) { devTab.showReport(json) }
                }
                Timer {
                    id: pulseTimer
                    interval: 2000
                    onTriggered: if (root.device) { root.device.setVibration(0); vibSlider.value = 0 }
                }
                ColumnLayout {
                    id: devCol2
                    width: parent.width; spacing: 14
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: statusRow.implicitHeight + 28
                        RowLayout {
                            id: statusRow
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: devTab.connected ? "#3ddc84" : (root.device && root.device.state === "connecting") ? Theme.orange : "#5d6b7a"
                            }
                            Text {
                                objectName: "deviceStatus"
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: !root.device || root.device.state === "disconnected" ? qsTr("Not connected — connect with the device chip at the top or in the Player's Inspector → Devices.")
                                      : root.device.state === "connecting" ? qsTr("Searching for device… Up to 20 seconds.")
                                      : [qsTr("Connected"), root.device.mock ? qsTr("Mock device") : root.device.name, root.device.address,
                                         root.device.batteryOk ? qsTr("Battery %1%").arg(root.device.batteryPercent) : ""].filter((t) => t !== "").join(" · ")
                                color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: testCol.implicitHeight + 28
                        opacity: devTab.canTest ? 1 : 0.5
                        ColumnLayout {
                            id: testCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Function test"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            Text {
                                textFormat: Text.PlainText
                                visible: devTab.connected && root.device.sessionActive
                                text: qsTr("Playback or training is running — the test waits until it stops.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                Text { textFormat: Text.PlainText; text: qsTr("Vibration"); Layout.preferredWidth: 90; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                                FlowSlider {
                                    id: vibSlider
                                    objectName: "testVibration"
                                    Layout.fillWidth: true
                                    enabled: devTab.canTest
                                    from: 0; to: 100
                                    onMoved: (v) => root.device.setVibration(v / 100)
                                }
                                Text {
                                    objectName: "testVibrationLabel"
                                    textFormat: Text.PlainText
                                    Layout.preferredWidth: 120
                                    text: qsTr("%1 % (level %2)").arg(Math.round(vibSlider.value)).arg(devTab.level(vibSlider.value, 10))
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true; spacing: 10
                                visible: !root.device || root.device.capSuction || !devTab.connected
                                Text { textFormat: Text.PlainText; text: qsTr("Suction"); Layout.preferredWidth: 90; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                                FlowSlider {
                                    id: sucSlider
                                    objectName: "testSuction"
                                    Layout.fillWidth: true
                                    enabled: devTab.canTest
                                    from: 0; to: 100
                                    onMoved: (v) => root.device.setSuction(v / 100)
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.preferredWidth: 120
                                    text: qsTr("%1 % (level %2)").arg(Math.round(sucSlider.value)).arg(devTab.level(sucSlider.value, 5))
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                            }
                            Row {
                                spacing: 8
                                FlowButton {
                                    objectName: "testPulse"
                                    text: qsTr("Short test pulse")
                                    enabled: devTab.canTest && !pulseTimer.running
                                    // 0.7: two seconds of medium vibration, then off.
                                    onClicked: { root.device.setVibration(0.5); vibSlider.value = 50; pulseTimer.restart() }
                                }
                                FlowButton {
                                    objectName: "testAllOff"
                                    text: qsTr("All off"); danger: true
                                    enabled: devTab.connected
                                    onClicked: { pulseTimer.stop(); root.device.stop(); vibSlider.value = 0; sucSlider.value = 0 }
                                }
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: rawCol.implicitHeight + 28
                        visible: !devTab.connected || root.device.capRaw
                        opacity: devTab.canTest ? 1 : 0.5
                        ColumnLayout {
                            id: rawCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Raw value test"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: qsTr("Sends the level value without conversion. 0–10 / 0–5 come from Buttplug — whether the firmware accepts more is unclear. Try 3 vs 4, then 20/50/100.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            RowLayout {
                                spacing: 8
                                FlowCombo {
                                    id: rawChannel
                                    objectName: "rawChannel"
                                    Layout.preferredWidth: 150
                                    textRole: "label"; valueRole: "value"
                                    model: [{ label: qsTr("Vibration"), value: "vibration" }, { label: qsTr("Suction"), value: "suction" }]
                                }
                                FlowTextField {
                                    id: rawValue
                                    objectName: "rawValue"
                                    implicitWidth: 90
                                    text: "0"
                                    maxLength: 3
                                }
                                FlowButton {
                                    objectName: "rawSend"
                                    text: qsTr("Send")
                                    enabled: devTab.canTest
                                    onClicked: root.device.testRawValue(rawChannel.currentValue, Math.max(0, Math.min(255, parseInt(rawValue.text) || 0)))
                                }
                                Text {
                                    objectName: "rawHint"
                                    textFormat: Text.PlainText
                                    readonly property int max: rawChannel.currentValue === "suction" ? 5 : 10
                                    text: (parseInt(rawValue.text) || 0) > max ? qsTr("above documented maximum (%1)").arg(max) : ""
                                    color: Theme.orange; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: diagCol.implicitHeight + 28
                        opacity: devTab.canTest || (root.device && root.device.diagnosticsRunning) ? 1 : 0.5
                        ColumnLayout {
                            id: diagCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Device diagnostics"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                text: qsTr("Automated test sequence (raw acceptance, update rate, channel interaction). Measures write latency and accepted values — not felt intensity. The device moves briefly several times.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                FlowButton {
                                    objectName: "diagRun"
                                    text: qsTr("Run diagnostics")
                                    enabled: devTab.canTest && !root.device.diagnosticsRunning
                                    onClicked: {
                                        devTab.report = null
                                        devTab.diagStatus = qsTr("Running… (vibration/suction move briefly several times)")
                                        root.device.runDiagnostics()
                                    }
                                }
                                FlowButton {
                                    text: qsTr("Cancel")
                                    visible: root.device !== null && root.device.diagnosticsRunning
                                    onClicked: root.device.cancelDiagnostics()
                                }
                            }
                            Text {
                                objectName: "diagStatus"
                                textFormat: Text.PlainText
                                visible: text !== ""
                                text: devTab.diagStatus
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Text {
                                textFormat: Text.PlainText
                                visible: devTab.report !== null && devTab.report.interrupted === true
                                text: qsTr("Run was interrupted, incomplete.")
                                color: Theme.orange; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            // 0.7 result table: Phase · Commands · Errors · Avg latency · Max latency.
                            GridLayout {
                                objectName: "diagTable"
                                visible: devTab.report !== null
                                columns: 5; columnSpacing: 18; rowSpacing: 4
                                Repeater {
                                    model: [qsTr("Phase"), qsTr("Commands"), qsTr("Errors"), qsTr("Avg latency"), qsTr("Max latency")]
                                    delegate: Text {
                                        required property string modelData
                                        textFormat: Text.PlainText; text: modelData
                                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12; font.weight: Font.Medium
                                    }
                                }
                                Repeater {
                                    objectName: "diagRows"
                                    model: {
                                        const out = []
                                        for (const p of (devTab.report ? devTab.report.phases || [] : []))
                                            out.push(p.phase.replace(/_/g, " "), String(p.commands), String(p.errors),
                                                     p.meanLatencyMs.toFixed(1) + " ms", p.maxLatencyMs.toFixed(1) + " ms")
                                        return out
                                    }
                                    delegate: Text {
                                        required property string modelData
                                        required property int index
                                        textFormat: Text.PlainText; text: modelData
                                        color: index % 5 === 2 && modelData !== "0" ? Theme.danger : Theme.text
                                        font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                }
                            }
                            Repeater {
                                model: devTab.report ? devTab.report.notes || [] : []
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText; text: modelData
                                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }

            // ---- Log (0.7 log.js) ----
            ColumnLayout {
                objectName: "logTab"
                visible: root.tab === 2
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    FlowToggle {
                        objectName: "warningsOnly"
                        checked: ctl.warningsOnly
                        onToggled: (c) => ctl.warningsOnly = c
                    }
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Warnings/errors only"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        id: logStatus
                        objectName: "logStatus"
                        textFormat: Text.PlainText
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    FlowButton {
                        objectName: "copyLog"
                        text: qsTr("Copy all")
                        onClicked: logStatus.text = ctl.copyLog() ? qsTr("Copied.") : qsTr("Nothing to copy.")
                    }
                    FlowButton {
                        objectName: "clearLog"
                        text: qsTr("Clear")
                        onClicked: { ctl.clearLog(); logStatus.text = qsTr("Cleared.") }
                    }
                    FlowButton { text: qsTr("Open folder"); enabled: ctl.logFile !== ""; onClicked: ctl.openLogFolder() }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: Qt.rgba(0, 0, 0, 0.25); border.color: Theme.cardBorder
                    ScrollView {
                        id: logScroll
                        anchors.fill: parent; anchors.margins: 8
                        TextArea {
                            id: logView
                            objectName: "logView"
                            readOnly: true; selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.NoWrap
                            text: ctl.logText !== "" ? ctl.logText : qsTr("Nothing logged yet.")
                            color: Theme.text; font.family: "monospace"; font.pixelSize: 12
                            background: null
                            // Newest at the bottom, as 0.7.
                            onTextChanged: cursorPosition = length
                        }
                    }
                }
            }

            // ---- About ----
            ColumnLayout {
                objectName: "aboutTab"
                visible: root.tab === 3
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 12
                RowLayout {
                    spacing: 16
                    Image { source: "assets/logo.png"; Layout.preferredWidth: 72; Layout.preferredHeight: 72; sourceSize: Qt.size(144, 144) }
                    ColumnLayout {
                        spacing: 4
                        Text { textFormat: Text.PlainText; text: "SamNPlayer 2"; color: Theme.orange; font.family: Theme.font; font.pixelSize: 26; font.weight: Font.DemiBold }
                        Text {
                            objectName: "aboutVersion"
                            textFormat: Text.PlainText
                            text: qsTr("Version %1 · free and open source (GPL-3.0-or-later)").arg(ctl.version !== "" ? ctl.version : "dev")
                            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: qsTr("Plays scripts with video and your device, creates them from a video, and guides training.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                    }
                }
                Row {
                    spacing: 8
                    FlowButton { objectName: "showLicence"; text: qsTr("Licence"); checked: legal.which === 0; onClicked: legal.which = 0 }
                    FlowButton { objectName: "showThirdParty"; text: qsTr("Third-party notices"); checked: legal.which === 1; onClicked: legal.which = 1 }
                }
                Rectangle {
                    id: legal
                    property int which: 0
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: Qt.rgba(0, 0, 0, 0.25); border.color: Theme.cardBorder
                    ScrollView {
                        anchors.fill: parent; anchors.margins: 8
                        TextArea {
                            objectName: "legalText"
                            readOnly: true; selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            text: legal.which === 0 ? ctl.licenceText : ctl.thirdPartyText
                            color: Theme.text; font.family: "monospace"; font.pixelSize: 12
                            background: null
                        }
                    }
                }
            }
        }
    }
}
