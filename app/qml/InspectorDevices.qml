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
    readonly property var dev: device
    signal resetTests()
    readonly property bool busy: dev.connecting
    // The core's drivers in the order of the choice: id (the kind),
    // name, help, needsAddress, addressExample, serialPort.
    readonly property var drivers: dev.drivers
    readonly property var kinds: drivers.map(d => d.id)
    readonly property string kind: kinds[transport.currentIndex] ?? ""
    readonly property var driver: drivers[transport.currentIndex] ?? ({})
    readonly property bool canTest: dev.connected && !dev.sessionActive
    function chosenTarget() {
        if (driver.serialPort) return ports.currentIndex >= 0 && device.serialPorts.length > 0 ? device.serialPorts[ports.currentIndex] : ""
        return target.text.trim()
    }
    function pickSavedPort() {
        const serial = drivers.find(d => d.serialPort)
        if (!serial) return
        const i = device.serialPorts.indexOf(device.savedTarget(serial.id))
        if (i >= 0) ports.currentIndex = i
    }
    Component.onCompleted: {
        // The last choice of the last successful connect (0.7 prefs).
        const i = kinds.indexOf(device.savedKind())
        transport.currentIndex = i >= 0 ? i : 0
        target.text = device.savedTarget(kind)
        if (driver.serialPort) device.refreshSerialPorts()
    }
    Connections {
        target: device
        function onSerialPortsChanged() { root.pickSavedPort() }
        // The choice shows what is connected (also after a connect from elsewhere).
        function onStatusChanged() {
            const i = root.kinds.indexOf(device.kind)
            if (device.connected && i >= 0 && i !== transport.currentIndex) transport.currentIndex = i
        }
    }

    InspectorCard {
        title: qsTr("Device")
        help: "dev-transport"
        // Status: dot, line and a short caption (0.7 wording).
        Row {
            spacing: 8
            Rectangle {
                y: 4; width: 10; height: 10; radius: 5
                color: root.dev.sessionActive || root.busy ? Theme.orange
                     : root.dev.connected ? Theme.good : Theme.textMuted
            }
            Text {
                textFormat: Text.PlainText
                id: statusLine
                width: root.width - 24 - 18
                wrapMode: Text.Wrap
                color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                text: {
                    const d = root.dev
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
        InspectorNote {
            text: root.dev.connecting ? qsTr("Up to 20 seconds.")
                : root.dev.connected ? (root.dev.sessionActive ? qsTr("Test again after the session ends.")
                    : root.dev.mock ? qsTr("Simulated device") : qsTr("Ready for function test."))
                : qsTr("Choose a connection and tap Connect.")
        }
        // Capability chips: each channel the device has.
        Flow {
            width: parent.width; spacing: 6
            visible: root.dev.connected
            Repeater {
                model: root.dev.connected ? root.dev.capabilities : []
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
                visible: root.dev.capBattery
                width: batText.implicitWidth + 16; height: 22; radius: 11
                color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.cardBorder
                Text { textFormat: Text.PlainText; id: batText; anchors.centerIn: parent; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11
                       text: root.dev.batteryOk ? qsTr("Battery %1%").arg(root.dev.batteryPercent) : qsTr("Battery") }
            }
            Rectangle {
                visible: root.dev.capRaw
                width: rawText.implicitWidth + 16; height: 22; radius: 11
                color: Qt.rgba(1, 1, 1, 0.05); border.color: Theme.cardBorder
                Text { textFormat: Text.PlainText; id: rawText; anchors.centerIn: parent; text: qsTr("Raw values"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 11 }
            }
        }
        FlowCombo {
            id: transport
            width: parent.width
            enabled: !root.dev.connected && !root.busy
            model: root.drivers.map(d => d.name)
            onActivated: {
                target.text = device.savedTarget(root.kind)
                if (root.driver.serialPort) { device.refreshSerialPorts(); root.pickSavedPort() }
            }
        }
        FlowTextField {
            id: target
            width: parent.width
            visible: root.driver.needsAddress === true
            enabled: !root.dev.connected && !root.busy
            maxLength: 200
            placeholder: root.driver.addressExample ?? ""
            onAccepted: if (connectBtn.enabled) connectBtn.clicked()
        }
        Row {
            visible: root.driver.serialPort === true
            spacing: 8
            FlowCombo {
                id: ports
                width: root.width - 24 - 90
                enabled: !root.dev.connected && !root.busy
                model: device.serialPorts.length > 0 ? device.serialPorts : [qsTr("No serial port found")]
            }
            FlowButton {
                text: qsTr("Refresh"); implicitHeight: 34
                enabled: !root.dev.connected && !root.busy
                onClicked: device.refreshSerialPorts()
                HoverHandler { id: refreshHover }
                ToolTipText { shown: refreshHover.hovered; text: qsTr("Look for serial ports again") }
            }
        }
        InspectorNote { text: root.driver.help ?? "" }
        Row {
            spacing: 8
            FlowButton {
                id: connectBtn
                text: qsTr("Connect"); primary: true
                enabled: !root.dev.connected && !root.busy && !root.dev.sessionActive
                onClicked: device.connectDevice(root.kind, root.chosenTarget())
            }
            FlowButton {
                text: root.busy ? qsTr("Cancel") : qsTr("Disconnect")
                enabled: (root.dev.connected || root.busy) && !root.dev.sessionActive
                onClicked: device.disconnectDevice()
            }
        }
        FlowButton {
            text: qsTr("Emergency stop"); danger: true
            enabled: root.dev.connected
            onClicked: ctrl.output.emergencyStop()
            HoverHandler { id: estopHover }
            ToolTipText { shown: estopHover.hovered; text: qsTr("Stops the device at once, drops queued commands and pauses the video.") }
        }
        Text {
            textFormat: Text.PlainText
            visible: device.lastError !== ""
            width: parent.width; wrapMode: Text.Wrap
            text: device.lastError
            color: Theme.danger; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        InspectorNote { visible: device.lastError !== "" && device.hint !== ""; text: device.hint }
    }
    InspectorCard {
        title: qsTr("Channels")
        InspectorChannel {
            ctrl: ctrl
            name: qsTr("Stroke"); dot: Theme.orange; value: ctrl.strokeNow 
        }
        InspectorChannel {
            ctrl: ctrl
            name: qsTr("Vibration"); dot: Theme.laneVibration; value: ctrl.vibrationNow 
        }
        InspectorChannel {
            ctrl: ctrl
            name: qsTr("Suction"); dot: Theme.laneSuction; value: ctrl.suctionNow 
        }
        InspectorNote { text: device.connected ? qsTr("Live values at the playhead. The lanes show the channels this device can use.")
                                          : qsTr("Live values at the playhead. Connect a device to send them.") }
    }
    // Vibration / suction output: on / off and level (0.7 SetChannelLevels).
    InspectorCard {
        title: qsTr("Output")
        visible: root.dev.connected && (root.dev.capVibration || root.dev.capSuction)
        InspectorOutputRow {
            visible: root.dev.capVibration
            name: qsTr("Vibration"); on: ctrl.output.vibrationOn; level: ctrl.output.vibrationLevel
            onToggled: ctrl.output.vibrationOn = !ctrl.output.vibrationOn
            onLevelPicked: (v) => ctrl.output.vibrationLevel = v
        }
        InspectorOutputRow {
            visible: root.dev.capSuction
            name: qsTr("Suction"); on: ctrl.output.suctionOn; level: ctrl.output.suctionLevel
            onToggled: ctrl.output.suctionOn = !ctrl.output.suctionOn
            onLevelPicked: (v) => ctrl.output.suctionLevel = v
        }
        // 0.7 Soft-Start (Playback Advanced) and Effects → Reduce peaks.
        InspectorSliderRow {
            label: qsTr("Soft start")
            help: "playback.soft-start"
            tip: qsTr("At Play, after a pause and after a quiet stretch, vibration and suction ramp up over this time in 10 steps instead of jumping to the script. Default 500 ms.")
            from: 0; to: 3000; value: ctrl.output.softStartMs
            valueText: ctrl.output.softStartMs === 0 ? qsTr("Off") : qsTr("%1 ms").arg(ctrl.output.softStartMs)
            onPicked: (v) => ctrl.output.softStartMs = Math.round(v / 100) * 100
        }
        FlowButton {
            text: qsTr("Reduce peaks"); checked: ctrl.output.reducePeaks
            onClicked: ctrl.output.reducePeaks = !ctrl.output.reducePeaks
            HoverHandler { id: peaksHover }
            ToolTipText { shown: peaksHover.hovered; text: qsTr("Softens vibration and suction above 60 %: full strength comes out at 76 %. Also while playing.") }
        }
        InspectorNote { text: qsTr("Scales what the script sends; the script file is not changed.") }
    }
    // A stroker (The Handy, OSR2 …): safety range and motion filter.
    InspectorCard {
        title: qsTr("Stroker")
        visible: root.dev.connected && root.dev.capLinear
        FlowButton {
            text: qsTr("Send stroke to the device"); checked: ctrl.output.strokeOn
            onClicked: ctrl.output.strokeOn = !ctrl.output.strokeOn
        }
        Row {
            spacing: 8; height: 26
            opacity: ctrl.output.strokeOn ? 1 : 0.45
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 52; text: qsTr("Bottom"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            FlowSlider {
                anchors.verticalCenter: parent.verticalCenter; width: 140; enabled: ctrl.output.strokeOn
                from: 0; to: 90; value: ctrl.output.strokeLo
                onMoved: (v) => ctrl.output.setStrokeRange(Math.round(v / 5) * 5, ctrl.output.strokeHi, true)
            }
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: ctrl.output.strokeLo + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
        }
        Row {
            spacing: 8; height: 26
            opacity: ctrl.output.strokeOn ? 1 : 0.45
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 52; text: qsTr("Top"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            FlowSlider {
                anchors.verticalCenter: parent.verticalCenter; width: 140; enabled: ctrl.output.strokeOn
                from: 10; to: 100; value: ctrl.output.strokeHi
                onMoved: (v) => ctrl.output.setStrokeRange(ctrl.output.strokeLo, Math.round(v / 5) * 5, false)
            }
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: ctrl.output.strokeHi + "%"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
        }
        InspectorNote { text: qsTr("Safety range: the device only moves inside it; the script's full stroke is scaled into it.") }
        Row {
            spacing: 8
            Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Motion"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                   HoverHandler { id: motionHover }
                   ToolTipText { shown: motionHover.hovered; text: qsTr("The device moves in straight lines between the points it gets. Smooth sends extra points along a soft curve through the script's points; Rounder rounds corners a little more. The script file is not changed.") } }
            FlowCombo {
                width: 170
                readonly property var values: ["", "pchip", "makima"]
                model: [qsTr("Raw script points"), qsTr("Smooth"), qsTr("Rounder")]
                currentIndex: Math.max(0, values.indexOf(ctrl.output.smooth))
                onActivated: (i) => ctrl.output.smooth = values[i]
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
                currentIndex: Math.max(0, values.indexOf(ctrl.output.maxSpeed))
                onActivated: (i) => ctrl.output.maxSpeed = values[i]
            }
        }
        Row {
            spacing: 8
            FlowButton {
                text: qsTr("Fill long pauses"); checked: ctrl.output.idleFill
                onClicked: ctrl.output.idleFill = !ctrl.output.idleFill
                HoverHandler { id: fillHover }
                ToolTipText { shown: fillHover.hovered; text: qsTr("Pauses of 4 s or more get a gentle wave, so the stroker does not stand still.") }
            }
            FlowButton {
                text: qsTr("Move home on pause"); checked: ctrl.output.homeOnPause
                onClicked: ctrl.output.homeOnPause = !ctrl.output.homeOnPause
                HoverHandler { id: homeHover }
                ToolTipText { shown: homeHover.hovered; text: qsTr("On pause or stop the stroker moves slowly to the bottom of its range.") }
            }
        }
    }
    // Function test: only with a device and while nothing plays.
    InspectorCard {
        title: qsTr("Function test")
        visible: root.dev.connected
        opacity: root.canTest ? 1 : 0.5
        Repeater {
            model: [
                { name: qsTr("Vibration"), has: root.dev.capVibration, steps: 10, key: "vibration" },
                { name: qsTr("Suction"), has: root.dev.capSuction, steps: 5, key: "suction" },
                { name: qsTr("Position"), has: root.dev.capLinear, steps: 0, key: "position" }
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
                    enabled: root.canTest
                    from: 0; to: 100; value: testRow.pct
                    onMoved: (v) => {
                        testRow.pct = Math.round(v)
                        if (testRow.modelData.key === "vibration") device.setVibration(testRow.pct / 100)
                        else if (testRow.modelData.key === "suction") device.setSuction(testRow.pct / 100)
                        else device.setPosition(testRow.pct / 100, 300)
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
            text: qsTr("All off"); danger: true; enabled: root.canTest
            onClicked: { device.stop(); root.resetTests() }
        }
        InspectorNote { text: root.dev.sessionActive ? qsTr("Playback or training is running — device test unavailable.")
                                              : qsTr("Moves the device now. Values between two levels have no effect.") }
    }
}
