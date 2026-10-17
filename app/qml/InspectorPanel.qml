import QtQuick
import SamN

// The Inspector of the Player (0.7 v1): tabs Devices · Audio · Playback ·
// View. Functions not built yet say which phase brings them; nothing pretends.
GlassCard {
    id: root
    required property PlayerController ctrl
    // The one device connection of the app (Main.qml).
    required property DeviceController device
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
            textFormat: Text.PlainText
            id: head
            x: 12; y: 10; text: parent.title
            color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        Column { id: body; x: 12; y: head.y + head.height + 8; width: parent.width - 24; spacing: 8 }
    }
    component Note: Text {
        textFormat: Text.PlainText
        width: parent ? parent.width : 260
        wrapMode: Text.Wrap
        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
    }
    // One output channel: on / off and a level (0.7 SetChannelLevels).
    component OutputRow: Row {
        property string name
        property bool on
        property int level
        signal toggled()
        signal levelPicked(int v)
        spacing: 8; height: 30
        FlowButton {
            width: 54; implicitHeight: 28; text: parent.on ? qsTr("On") : qsTr("Off"); checked: parent.on
            onClicked: parent.toggled()
        }
        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 62; text: parent.name; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
        FlowSlider {
            anchors.verticalCenter: parent.verticalCenter; width: 92
            enabled: parent.on; opacity: enabled ? 1 : 0.45
            from: 0; to: 100; value: parent.level
            onMoved: (v) => parent.levelPicked(Math.round(v / 5) * 5)
        }
        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: parent.level + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
    }
    component Channel: Item {
        property string name
        property color dot
        property int value
        width: parent ? parent.width : 260; height: 26
        Rectangle { y: 9; width: 8; height: 8; radius: 4; color: parent.dot }
        Text { textFormat: Text.PlainText; x: 16; anchors.verticalCenter: parent.verticalCenter; text: parent.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
        Rectangle {
            x: 100; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 150; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle { width: parent.width * parent.parent.value / 100; height: parent.height; radius: 3; color: parent.parent.dot
                Behavior on width { NumberAnimation { duration: 90 } } }
        }
        Text {
            textFormat: Text.PlainText
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
        textFormat: Text.PlainText
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

            // Devices (0.7 Tools → Device and the Player inspector): connection,
            // live channels, output levels, the stroker's range and filter, and
            // the function test.
            Column {
                id: devices
                visible: tabs.current === 0
                width: parent.width; spacing: 10
                readonly property var dev: root.device
                signal resetTests()
                readonly property bool busy: dev.connecting
                readonly property var kinds: ["ble", "tcode", "tcode-serial", "intiface", "mock"]
                readonly property string kind: kinds[transport.currentIndex]
                readonly property bool canTest: dev.connected && !dev.sessionActive
                readonly property var hints: ({
                    "ble": qsTr("Searches up to 20 seconds for a device named “Sam Neo 2”. The device must be on and not connected to another app."),
                    "tcode": qsTr("Sends TCode to an OSR2 / SR6 (or other TCode device) running Wi-Fi firmware. Enter its IP address; plain addresses use UDP port 8000, or write tcp://host:port. UDP has no handshake: if the device does not move, check the address and firmware settings."),
                    "tcode-serial": qsTr("Sends TCode to an OSR2 / SR6 (or other TCode device) over its USB cable at 115200 baud. Choose its port. Close other programs that use the port (e.g. MultiFunPlayer) first."),
                    "intiface": qsTr("Connects via a running Buttplug server. Start Intiface Central, connect the device there and start the server. Leave the address empty for ws://127.0.0.1:12345."),
                    "mock": qsTr("Simulates a device to test the UI without hardware.")
                })
                function chosenTarget() {
                    if (kind === "tcode-serial") return ports.currentIndex >= 0 && root.device.serialPorts.length > 0 ? root.device.serialPorts[ports.currentIndex] : ""
                    return target.text.trim()
                }
                function pickSavedPort() {
                    const i = root.device.serialPorts.indexOf(root.device.savedTarget("tcode-serial"))
                    if (i >= 0) ports.currentIndex = i
                }
                Component.onCompleted: {
                    // The last choice of the last successful connect (0.7 prefs).
                    const i = kinds.indexOf(root.device.savedKind())
                    transport.currentIndex = i >= 0 ? i : 0
                    target.text = root.device.savedTarget(kind)
                    if (kind === "tcode-serial") root.device.refreshSerialPorts()
                }
                Connections {
                    target: root.device
                    function onSerialPortsChanged() { devices.pickSavedPort() }
                    // The choice shows what is connected (also after a connect from elsewhere).
                    function onStatusChanged() {
                        const i = devices.kinds.indexOf(root.device.kind)
                        if (root.device.connected && i >= 0 && i !== transport.currentIndex) transport.currentIndex = i
                    }
                }

                Card {
                    title: qsTr("Device")
                    // Status: dot, line and a short caption (0.7 wording).
                    Row {
                        spacing: 8
                        Rectangle {
                            y: 4; width: 10; height: 10; radius: 5
                            color: devices.dev.sessionActive || devices.busy ? Theme.orange
                                 : devices.dev.connected ? Theme.good : Theme.textMuted
                        }
                        Text {
                            textFormat: Text.PlainText
                            id: statusLine
                            width: devices.width - 24 - 18
                            wrapMode: Text.Wrap
                            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                            text: {
                                const d = devices.dev
                                if (d.connected && d.sessionActive) return qsTr("Playback or training is running — device test unavailable.")
                                if (d.connecting) return qsTr("Searching for device…")
                                if (!d.connected) return qsTr("Not connected")
                                const parts = [d.mock ? qsTr("Mock device connected") : qsTr("Connected")]
                                if (d.name !== "") parts.push(d.name)
                                if (d.address !== "") parts.push(d.address)
                                if (d.rssi !== 0) parts.push(qsTr("Signal %1 dBm").arg(d.rssi))
                                if (d.batteryOk) parts.push(qsTr("Battery %1%").arg(d.batteryPercent))
                                return parts.join("  ·  ")
                            }
                        }
                    }
                    Note {
                        text: devices.dev.connecting ? qsTr("Up to 20 seconds.")
                            : devices.dev.connected ? (devices.dev.sessionActive ? qsTr("Test again after the session ends.")
                                : devices.dev.mock ? qsTr("Simulated device") : qsTr("Ready for function test."))
                            : qsTr("Choose a connection and tap Connect.")
                    }
                    // Capability chips: each channel the device has.
                    Flow {
                        width: parent.width; spacing: 6
                        visible: devices.dev.connected
                        Repeater {
                            model: devices.dev.connected ? devices.dev.capabilities : []
                            delegate: Rectangle {
                                required property var modelData
                                readonly property string label: {
                                    const c = String(modelData.channel)
                                    const n = c.charAt(0).toUpperCase() + c.slice(1)
                                    return modelData.driven ? n : qsTr("%1 · coming").arg(n)
                                }
                                width: chipText.implicitWidth + 16; height: 22; radius: 11
                                color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.cardBorder
                                opacity: modelData.driven ? 1 : 0.55
                                Text { textFormat: Text.PlainText; id: chipText; anchors.centerIn: parent; text: parent.label; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                                HoverHandler { id: chipHover }
                                ToolTipText { shown: chipHover.hovered && !parent.modelData.driven; text: qsTr("The device has this channel; SamNPlayer does not drive it yet.") }
                            }
                        }
                        Rectangle {
                            visible: devices.dev.capBattery
                            width: batText.implicitWidth + 16; height: 22; radius: 11
                            color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.cardBorder
                            Text { textFormat: Text.PlainText; id: batText; anchors.centerIn: parent; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11
                                   text: devices.dev.batteryOk ? qsTr("Battery %1%").arg(devices.dev.batteryPercent) : qsTr("Battery") }
                        }
                        Rectangle {
                            visible: devices.dev.capRaw
                            width: rawText.implicitWidth + 16; height: 22; radius: 11
                            color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.cardBorder
                            Text { textFormat: Text.PlainText; id: rawText; anchors.centerIn: parent; text: qsTr("Raw values"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
                        }
                    }
                    FlowCombo {
                        id: transport
                        width: parent.width
                        enabled: !devices.dev.connected && !devices.busy
                        model: [qsTr("Direct via Bluetooth (Sam Neo 2)"), qsTr("TCode over Wi-Fi (OSR2 / SR6)"),
                                qsTr("TCode over USB (OSR2 / SR6)"), qsTr("Via Intiface Central"), qsTr("Mock device (no hardware)")]
                        onActivated: {
                            target.text = root.device.savedTarget(devices.kind)
                            if (devices.kind === "tcode-serial") { root.device.refreshSerialPorts(); devices.pickSavedPort() }
                        }
                    }
                    FlowTextField {
                        id: target
                        width: parent.width
                        visible: devices.kind === "tcode" || devices.kind === "intiface"
                        enabled: !devices.dev.connected && !devices.busy
                        maxLength: 200
                        placeholder: devices.kind === "intiface" ? qsTr("e.g. 192.168.1.50 or empty = this machine")
                                                                  : qsTr("e.g. 192.168.1.50 or tcp://osr.local:8000")
                        onAccepted: if (connectBtn.enabled) connectBtn.clicked()
                    }
                    Row {
                        visible: devices.kind === "tcode-serial"
                        spacing: 8
                        FlowCombo {
                            id: ports
                            width: devices.width - 24 - 90
                            enabled: !devices.dev.connected && !devices.busy
                            model: root.device.serialPorts.length > 0 ? root.device.serialPorts : [qsTr("No serial port found")]
                        }
                        FlowButton {
                            text: qsTr("Refresh"); implicitHeight: 34
                            enabled: !devices.dev.connected && !devices.busy
                            onClicked: root.device.refreshSerialPorts()
                            HoverHandler { id: refreshHover }
                            ToolTipText { shown: refreshHover.hovered; text: qsTr("Look for serial ports again") }
                        }
                    }
                    Note { text: devices.hints[devices.kind] }
                    Row {
                        spacing: 8
                        FlowButton {
                            id: connectBtn
                            text: qsTr("Connect"); primary: true
                            enabled: !devices.dev.connected && !devices.busy && !devices.dev.sessionActive
                            onClicked: root.device.connectDevice(devices.kind, devices.chosenTarget())
                        }
                        FlowButton {
                            text: devices.busy ? qsTr("Cancel") : qsTr("Disconnect")
                            enabled: (devices.dev.connected || devices.busy) && !devices.dev.sessionActive
                            onClicked: root.device.disconnectDevice()
                        }
                    }
                    FlowButton {
                        text: qsTr("Emergency stop"); danger: true
                        enabled: devices.dev.connected
                        onClicked: root.ctrl.output.emergencyStop()
                        HoverHandler { id: estopHover }
                        ToolTipText { shown: estopHover.hovered; text: qsTr("Stops the device at once, drops queued commands and pauses the video.") }
                    }
                    Text {
                        textFormat: Text.PlainText
                        visible: root.device.lastError !== ""
                        width: parent.width; wrapMode: Text.Wrap
                        text: root.device.lastError
                        color: Theme.danger; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Note { visible: root.device.lastError !== "" && root.device.hint !== ""; text: root.device.hint }
                }
                Card {
                    title: qsTr("Channels")
                    Channel { name: qsTr("Stroke"); dot: Theme.orange; value: root.ctrl.strokeNow }
                    Channel { name: qsTr("Vibration"); dot: Theme.laneVibration; value: root.ctrl.vibrationNow }
                    Channel { name: qsTr("Suction"); dot: Theme.laneSuction; value: root.ctrl.suctionNow }
                    Note { text: root.device.connected ? qsTr("Live values at the playhead. The lanes show the channels this device can use.")
                                                      : qsTr("Live values at the playhead. Connect a device to send them.") }
                }
                // Vibration / suction output: on / off and level (0.7 SetChannelLevels).
                Card {
                    title: qsTr("Output")
                    visible: devices.dev.connected && (devices.dev.capVibration || devices.dev.capSuction)
                    OutputRow {
                        visible: devices.dev.capVibration
                        name: qsTr("Vibration"); on: root.ctrl.output.vibrationOn; level: root.ctrl.output.vibrationLevel
                        onToggled: root.ctrl.output.vibrationOn = !root.ctrl.output.vibrationOn
                        onLevelPicked: (v) => root.ctrl.output.vibrationLevel = v
                    }
                    OutputRow {
                        visible: devices.dev.capSuction
                        name: qsTr("Suction"); on: root.ctrl.output.suctionOn; level: root.ctrl.output.suctionLevel
                        onToggled: root.ctrl.output.suctionOn = !root.ctrl.output.suctionOn
                        onLevelPicked: (v) => root.ctrl.output.suctionLevel = v
                    }
                    Note { text: qsTr("Scales what the script sends; the script file is not changed.") }
                }
                // A stroker (The Handy, OSR2 …): safety range and motion filter.
                Card {
                    title: qsTr("Stroker")
                    visible: devices.dev.connected && devices.dev.capLinear
                    FlowButton {
                        text: qsTr("Send stroke to the device"); checked: root.ctrl.output.strokeOn
                        onClicked: root.ctrl.output.strokeOn = !root.ctrl.output.strokeOn
                    }
                    Row {
                        spacing: 8; height: 26
                        opacity: root.ctrl.output.strokeOn ? 1 : 0.45
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 52; text: qsTr("Bottom"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        FlowSlider {
                            anchors.verticalCenter: parent.verticalCenter; width: 140; enabled: root.ctrl.output.strokeOn
                            from: 0; to: 90; value: root.ctrl.output.strokeLo
                            onMoved: (v) => root.ctrl.output.setStrokeRange(Math.round(v / 5) * 5, root.ctrl.output.strokeHi, true)
                        }
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: root.ctrl.output.strokeLo + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                    Row {
                        spacing: 8; height: 26
                        opacity: root.ctrl.output.strokeOn ? 1 : 0.45
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 52; text: qsTr("Top"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        FlowSlider {
                            anchors.verticalCenter: parent.verticalCenter; width: 140; enabled: root.ctrl.output.strokeOn
                            from: 10; to: 100; value: root.ctrl.output.strokeHi
                            onMoved: (v) => root.ctrl.output.setStrokeRange(root.ctrl.output.strokeLo, Math.round(v / 5) * 5, false)
                        }
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: root.ctrl.output.strokeHi + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    }
                    Note { text: qsTr("Safety range: the device only moves inside it; the script's full stroke is scaled into it.") }
                    Row {
                        spacing: 8
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Motion"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                               HoverHandler { id: motionHover }
                               ToolTipText { shown: motionHover.hovered; text: qsTr("The device moves in straight lines between the points it gets. Smooth sends extra points along a soft curve through the script's points; Rounder rounds corners a little more. The script file is not changed.") } }
                        FlowCombo {
                            width: 170
                            readonly property var values: ["", "pchip", "makima"]
                            model: [qsTr("Raw script points"), qsTr("Smooth"), qsTr("Rounder")]
                            currentIndex: Math.max(0, values.indexOf(root.ctrl.output.smooth))
                            onActivated: (i) => root.ctrl.output.smooth = values[i]
                        }
                    }
                    Row {
                        spacing: 8
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Speed limit"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                               HoverHandler { id: speedHover }
                               ToolTipText { shown: speedHover.hovered; text: qsTr("No move faster than this; a too fast stroke stops short instead of slamming. In script units (0–100) per second.") } }
                        FlowCombo {
                            width: 170
                            readonly property var values: [0, 600, 400, 250]
                            model: [qsTr("No limit"), qsTr("Fast (600/s)"), qsTr("Medium (400/s)"), qsTr("Gentle (250/s)")]
                            currentIndex: Math.max(0, values.indexOf(root.ctrl.output.maxSpeed))
                            onActivated: (i) => root.ctrl.output.maxSpeed = values[i]
                        }
                    }
                    Row {
                        spacing: 8
                        FlowButton {
                            text: qsTr("Fill long pauses"); checked: root.ctrl.output.idleFill
                            onClicked: root.ctrl.output.idleFill = !root.ctrl.output.idleFill
                            HoverHandler { id: fillHover }
                            ToolTipText { shown: fillHover.hovered; text: qsTr("Pauses of 4 s or more get a gentle wave, so the stroker does not stand still.") }
                        }
                        FlowButton {
                            text: qsTr("Move home on pause"); checked: root.ctrl.output.homeOnPause
                            onClicked: root.ctrl.output.homeOnPause = !root.ctrl.output.homeOnPause
                            HoverHandler { id: homeHover }
                            ToolTipText { shown: homeHover.hovered; text: qsTr("On pause or stop the stroker moves slowly to the bottom of its range.") }
                        }
                    }
                }
                // Function test: only with a device and while nothing plays.
                Card {
                    title: qsTr("Function test")
                    visible: devices.dev.connected
                    opacity: devices.canTest ? 1 : 0.5
                    Repeater {
                        model: [
                            { name: qsTr("Vibration"), has: devices.dev.capVibration, steps: 10, key: "vibration" },
                            { name: qsTr("Suction"), has: devices.dev.capSuction, steps: 5, key: "suction" },
                            { name: qsTr("Position"), has: devices.dev.capLinear, steps: 0, key: "position" }
                        ]
                        delegate: Row {
                            id: testRow
                            required property var modelData
                            property int pct: 0
                            Connections { target: devices; function onResetTests() { testRow.pct = 0 } }
                            visible: modelData.has
                            spacing: 8; height: 26
                            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 62; text: testRow.modelData.name; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                            FlowSlider {
                                anchors.verticalCenter: parent.verticalCenter; width: 120
                                enabled: devices.canTest
                                from: 0; to: 100; value: testRow.pct
                                onMoved: (v) => {
                                    testRow.pct = Math.round(v)
                                    if (testRow.modelData.key === "vibration") root.device.setVibration(testRow.pct / 100)
                                    else if (testRow.modelData.key === "suction") root.device.setSuction(testRow.pct / 100)
                                    else root.device.setPosition(testRow.pct / 100, 300)
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                anchors.verticalCenter: parent.verticalCenter
                                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                                text: testRow.modelData.steps > 0 ? qsTr("%1 % (level %2)").arg(testRow.pct).arg(Math.round(testRow.pct / 100 * testRow.modelData.steps))
                                                                  : testRow.pct + " %"
                            }
                        }
                    }
                    FlowButton {
                        text: qsTr("All off"); danger: true; enabled: devices.canTest
                        onClicked: { root.device.stop(); devices.resetTests() }
                    }
                    Note { text: devices.dev.sessionActive ? qsTr("Playback or training is running — device test unavailable.")
                                                          : qsTr("Moves the device now. Values between two levels have no effect.") }
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
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Script offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        OffsetField { value: root.ctrl.offsetMs; onChanged: (v) => root.ctrl.offsetMs = v }
                    }
                    Row {
                        spacing: 8
                        FlowButton { text: qsTr("Reset"); enabled: root.ctrl.offsetMs !== 0; onClicked: root.ctrl.offsetMs = 0 }
                        FlowButton { text: qsTr("Bake offset"); enabled: false }
                    }
                    Note { text: qsTr("Positive: the script comes later. Saved per script. Baking it into the script's times (all channels, bookmarks and chapters in one safe step) comes with the Editor (P4).") }
                }
                // Script Doctor (0.7 pb-quality): signal quality from the script alone.
                Card {
                    id: doctor
                    title: qsTr("Script Doctor")
                    property var result: null
                    FlowButton {
                        text: qsTr("Check script"); enabled: root.ctrl.hasScript
                        onClicked: doctor.result = root.ctrl.scriptQuality()
                    }
                    Rectangle {
                        visible: doctor.result !== null
                        width: parent.width; radius: 8
                        height: doctorText.implicitHeight + 16
                        readonly property bool failed: doctor.result !== null && (doctor.result.error !== "" || !doctor.result.passed)
                        color: failed ? Qt.rgba(216 / 255, 77 / 255, 77 / 255, 0.12) : Qt.rgba(61 / 255, 216 / 255, 117 / 255, 0.12)
                        border.color: failed ? "#d84d4d" : "#3dd875"
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
                    Connections { target: root.ctrl; function onScriptChanged() { doctor.result = null } }
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
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: Math.round(root.ctrl.speedHighlightThresh); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
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
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Intensity"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        FlowSlider {
                            anchors.verticalCenter: parent.verticalCenter; width: 130
                            from: 0; to: 1; value: root.ctrl.oMarkerIntensity
                            onMoved: (v) => root.ctrl.oMarkerIntensity = Math.round(v * 20) / 20
                        }
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: Math.round(root.ctrl.oMarkerIntensity * 100) + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
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
