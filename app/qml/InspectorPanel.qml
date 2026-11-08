import QtQuick
import QtQuick.Dialogs
import SamN

// The Inspector of the Player (0.7 v1): tabs Devices · Audio · Playback · View.
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
    // Edit the open script in the Generator's Editor (the scenes when true).
    signal editRequested(bool scenes)
    property string projectStatus: ""
    width: collapsed ? 44 : 320
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    function laneTitle(lane) {
        return ({
            stroke: qsTr("Stroke"), vibration: qsTr("Vibration"), suction: qsTr("Suction"),
            surge: qsTr("Surge"), sway: qsTr("Sway"), twist: qsTr("Twist"),
            roll: qsTr("Roll"), pitch: qsTr("Pitch")
        })[lane] || lane
    }
    function axisTitle(axis) { return laneTitle(axis) }

    component Card: Rectangle {
        id: card
        default property alias content: body.data
        property string title
        // A 0.7 help key (HelpTexts.js): a "?" after the title.
        property string help
        width: parent ? parent.width : 280
        height: body.implicitHeight + head.height + 22
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 1, 1, 0.025); border.color: Theme.cardBorder
        // Soft top catch-light (matches GlassCard).
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 1; height: 1; radius: 1
            color: Qt.rgba(180 / 255, 220 / 255, 255 / 255, 0.08)
        }
        Text {
            textFormat: Text.PlainText
            id: head
            x: 12; y: 10; text: card.title
            color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
        }
        HelpChip { key: card.help; visible: key !== "" && text !== ""; x: head.x + head.implicitWidth + 6; y: head.y + (head.height - height) / 2 }
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
    // A labelled slider with its value (0.7 range + readout).
    component SliderRow: Row {
        id: sr
        property string label
        property string tip
        property string help
        property real from: 0
        property real to: 1
        property real value: 0
        property string valueText
        property bool active: true
        signal picked(real v)
        spacing: 8; height: 26
        Text {
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter; width: 78
            text: sr.label; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            HoverHandler { id: srHover }
            ToolTipText { shown: srHover.hovered && sr.tip !== ""; text: sr.tip }
        }
        FlowSlider {
            anchors.verticalCenter: parent.verticalCenter; width: 112
            enabled: sr.active; opacity: enabled ? 1 : 0.45
            from: sr.from; to: sr.to; value: sr.value
            // The knob follows the model again (a preset may change it later).
            onMoved: (v) => { sr.picked(v); value = Qt.binding(() => sr.value) }
        }
        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: sr.valueText; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
        HelpChip { key: sr.help; visible: key !== "" && text !== ""; anchors.verticalCenter: parent.verticalCenter }
    }
    component Channel: Item {
        id: ch
        property string name
        property color dot
        property int value
        width: parent ? parent.width : 260; height: 26
        Rectangle {
            y: 9; width: 8; height: 8; radius: 4; color: ch.dot
            SequentialAnimation on opacity {
                running: ch.value > 0 && Qt.application.state === Qt.ApplicationActive && !Theme.reducedMotion
                loops: Animation.Infinite
                NumberAnimation { from: 0.55; to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.55; duration: 900; easing.type: Easing.InOutSine }
            }
        }
        Text { textFormat: Text.PlainText; x: 16; anchors.verticalCenter: parent.verticalCenter; text: ch.name; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
        Rectangle {
            x: 100; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 150; height: 6; radius: 3; color: Qt.rgba(1, 1, 1, 0.08)
            Rectangle {
                id: fill
                width: parent.width * ch.value / 100; height: parent.height; radius: 3; color: ch.dot
                Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width + 2; height: parent.height + 4; radius: 4; z: -1
                    color: ch.dot; opacity: ch.value > 0 ? 0.28 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.animFast } }
                }
            }
        }
        Text {
            textFormat: Text.PlainText
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            text: root.ctrl.hasScript ? ch.value : "–"; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
    }

    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Icon { anchors.centerIn: parent; name: root.collapsed ? "chevronLeft" : "chevronRight"; width: 16; height: 16; color: Theme.textDim }
        MouseArea {
            id: tg
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.collapsed = !root.collapsed
            HoverHandler { id: collapseHover }
            ToolTipText {
                shown: collapseHover.hovered
                text: root.collapsed
                      ? qsTr("Show Inspector — or press T for theatre (panels away)")
                      : qsTr("Hide Inspector — bigger video (T = theatre, panels away)")
            }
        }
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
                // The core's drivers in the order of the choice: id (the kind),
                // name, help, needsAddress, addressExample, serialPort.
                readonly property var drivers: dev.drivers
                readonly property var kinds: drivers.map(d => d.id)
                readonly property string kind: kinds[transport.currentIndex] ?? ""
                readonly property var driver: drivers[transport.currentIndex] ?? ({})
                readonly property bool canTest: dev.connected && !dev.sessionActive
                function chosenTarget() {
                    if (driver.serialPort) return ports.currentIndex >= 0 && root.device.serialPorts.length > 0 ? root.device.serialPorts[ports.currentIndex] : ""
                    return target.text.trim()
                }
                function pickSavedPort() {
                    const serial = drivers.find(d => d.serialPort)
                    if (!serial) return
                    const i = root.device.serialPorts.indexOf(root.device.savedTarget(serial.id))
                    if (i >= 0) ports.currentIndex = i
                }
                Component.onCompleted: {
                    // The last choice of the last successful connect (0.7 prefs).
                    const i = kinds.indexOf(root.device.savedKind())
                    transport.currentIndex = i >= 0 ? i : 0
                    target.text = root.device.savedTarget(kind)
                    if (driver.serialPort) root.device.refreshSerialPorts()
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
                    help: "dev-transport"
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
                        model: devices.drivers.map(d => d.name)
                        onActivated: {
                            target.text = root.device.savedTarget(devices.kind)
                            if (devices.driver.serialPort) { root.device.refreshSerialPorts(); devices.pickSavedPort() }
                        }
                    }
                    FlowTextField {
                        id: target
                        width: parent.width
                        visible: devices.driver.needsAddress === true
                        enabled: !devices.dev.connected && !devices.busy
                        maxLength: 200
                        placeholder: devices.driver.addressExample ?? ""
                        onAccepted: if (connectBtn.enabled) connectBtn.clicked()
                    }
                    Row {
                        visible: devices.driver.serialPort === true
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
                    Note { text: devices.driver.help ?? "" }
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
                    // 0.7 Soft-Start (Playback Advanced) and Effects → Reduce peaks.
                    SliderRow {
                        label: qsTr("Soft start")
                        help: "playback.soft-start"
                        tip: qsTr("At Play, after a pause and after a quiet stretch, vibration and suction ramp up over this time in 10 steps instead of jumping to the script. Default 500 ms.")
                        from: 0; to: 3000; value: root.ctrl.output.softStartMs
                        valueText: root.ctrl.output.softStartMs === 0 ? qsTr("Off") : qsTr("%1 ms").arg(root.ctrl.output.softStartMs)
                        onPicked: (v) => root.ctrl.output.softStartMs = Math.round(v / 100) * 100
                    }
                    FlowButton {
                        text: qsTr("Reduce peaks"); checked: root.ctrl.output.reducePeaks
                        onClicked: root.ctrl.output.reducePeaks = !root.ctrl.output.reducePeaks
                        HoverHandler { id: peaksHover }
                        ToolTipText { shown: peaksHover.hovered; text: qsTr("Softens vibration and suction above 60 %: full strength comes out at 76 %. Also while playing.") }
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
                    help: "playback.bpm"
                    Row {
                        spacing: 8
                        FlowTextField {
                            objectName: "bpmField"
                            width: 90
                            placeholder: root.ctrl.bpm > 0 ? String(root.ctrl.bpm)
                                       : root.ctrl.effectiveBpm > 0 ? qsTr("auto %1").arg(root.ctrl.effectiveBpm)
                                                                   : qsTr("20–240")
                            onAccepted: root.ctrl.bpm = parseFloat(text) || 0
                        }
                        FlowButton {
                            objectName: "bpmGrid"
                            text: qsTr("BPM grid")
                            checked: root.ctrl.bpmGrid
                            onClicked: root.ctrl.bpmGrid = !root.ctrl.bpmGrid
                        }
                    }
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "bpmFromStroke"
                            text: qsTr("Guess from stroke")
                            enabled: root.ctrl.hasScript
                            onClicked: root.ctrl.guessBpmFromStroke()
                        }
                        FlowButton {
                            objectName: "bpmFromAudio"
                            text: qsTr("From audio")
                            enabled: root.ctrl.videoName !== ""
                            onClicked: root.ctrl.guessBpmFromAudio()
                        }
                    }
                    Note { text: qsTr("Beat lines on the curve (every 4th is a bar). Display only. Leave BPM blank to use the stroke's own tempo when the grid is on. From audio measures the linked video's sound (first 10 min).") }
                    Note { objectName: "bpmNote"; visible: root.ctrl.toolNote !== "" && (root.ctrl.toolNote.indexOf("BPM") >= 0 || root.ctrl.toolNote.indexOf("audio") >= 0 || root.ctrl.toolNote.indexOf("sound") >= 0); text: root.ctrl.toolNote }
                }
                Card {
                    title: qsTr("Sound Curve")
                    help: "pb-aa-write"
                    Note { text: qsTr("Write the video's loudness into the script as the Vibration curve (smoothed). Playback then uses axes mode. Undo restores the previous vibration for this script.") }
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "soundCurveWriteInspector"
                            text: qsTr("Sound curve")
                            enabled: root.ctrl.hasScript && root.ctrl.videoName !== ""
                            onClicked: root.ctrl.proposeSoundCurve()
                        }
                        FlowButton {
                            objectName: "soundCurveUndoInspector"
                            text: qsTr("Undo")
                            visible: root.ctrl.canUndoSoundCurve
                            onClicked: root.ctrl.undoSoundCurve()
                        }
                    }
                    Note {
                        objectName: "soundCurveNote"
                        visible: root.ctrl.toolNote !== "" && root.ctrl.toolNote.indexOf("Sound") >= 0
                        text: root.ctrl.toolNote
                    }
                }
                Card {
                    title: qsTr("Audio Vibration")
                    Note { text: qsTr("Live vibration that follows the sound while playing is separate. For a denser bake tied to mapped frames, use the Editor (\"Vibration from the sound\"). For Sam Neo 2 feel lanes from the stroke, use Playback → Adapt for device.") }
                    FlowButton { objectName: "editForSound"; text: qsTr("Open in the Editor"); enabled: root.ctrl.hasScript; onClicked: root.editRequested(false) }
                }
            }
            // Playback: adapt finished scripts for toys, then script tools.
            Column {
                id: playback
                visible: tabs.current === 2
                width: parent.width; spacing: 10
                Card {
                    id: adapt
                    objectName: "adaptCard"
                    title: qsTr("Adapt for device")
                    help: "pb-optimize-target"
                    readonly property var targets: ["general", "neo2"]
                    readonly property int preferredTarget: (root.device && root.device.connected && root.device.kind === "samneo2") ? 1 : 0
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
                            enabled: root.ctrl.hasScript
                            onClicked: adaptConfirm.open()
                        }
                    }
                    FlowButton {
                        objectName: "adaptEditTools"
                        text: qsTr("More tools in the Editor…")
                        icon: "edit"
                        enabled: root.ctrl.hasScript
                        onClicked: root.editRequested(false)
                    }
                    Note { text: qsTr("Optimize saves into the script (and a .samn Emotion Script for Neo 2). Bake & share below writes feel channels or a community .funscript. Effects stay live playback feel only.") }
                    Note { objectName: "adaptNote"; visible: root.ctrl.toolNote !== ""; text: root.ctrl.toolNote }
                    MessageDialog {
                        id: adaptConfirm
                        title: qsTr("Optimize & save")
                        text: adaptTarget.currentIndex === 1
                              ? qsTr("Heal the stroke, add contact vibration and bake Neo 2 vibration / suction into the Emotion Script beside this file?")
                              : qsTr("Fill and heal stroke gaps and save the script so it plays cleanly on any device?")
                        buttons: MessageDialog.Ok | MessageDialog.Cancel
                        onAccepted: root.ctrl.optimizeFor(adapt.targets[adaptTarget.currentIndex])
                    }
                }
                // 0.7 Tools "Bake & share": bake Neo feel channels + community export.
                Card {
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
                        enabled: root.ctrl.hasScript
                        onClicked: bakeFeelConfirm.open()
                    }
                    FlowButton {
                        objectName: "shareForApps"
                        text: qsTr("Share for other apps")
                        icon: "download"
                        enabled: root.ctrl.hasScript
                        onClicked: {
                            const path = root.ctrl.exportFunscript()
                            if (path === "") root.projectStatus = root.ctrl.lastError
                        }
                    }
                    Note { objectName: "bakeShareNote"; visible: root.ctrl.toolNote !== ""; text: root.ctrl.toolNote }
                    MessageDialog {
                        id: bakeFeelConfirm
                        objectName: "bakeFeelConfirm"
                        title: qsTr("Bake feel channels")
                        text: qsTr("Write vibration and suction lanes from the stroke into the Emotion Script (.samn) beside this file?")
                        buttons: MessageDialog.Ok | MessageDialog.Cancel
                        onAccepted: root.ctrl.axesFromStroke()
                    }
                }
                Card {
                    id: scriptTools
                    title: qsTr("Script Tools")
                    help: "playback.script-offset"
                    Row {
                        spacing: 8
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Script offset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                        OffsetField { value: root.ctrl.offsetMs; onChanged: (v) => root.ctrl.offsetMs = v }
                    }
                    Row {
                        spacing: 8
                        FlowButton { text: qsTr("Reset"); enabled: root.ctrl.offsetMs !== 0; onClicked: root.ctrl.offsetMs = 0 }
                        FlowButton {
                            objectName: "bakeOffset"
                            text: qsTr("Bake offset")
                            enabled: root.ctrl.offsetMs !== 0 && root.ctrl.scriptPath !== ""
                            onClicked: bakeConfirm.open()
                        }
                        // 0.7 asked first: this rewrites the script file.
                        MessageDialog {
                            id: bakeConfirm
                            title: qsTr("Bake offset")
                            text: qsTr("Move every point, bookmark and chapter by %1 ms and save the script? The offset then goes back to 0.").arg(root.ctrl.offsetMs)
                            buttons: MessageDialog.Ok | MessageDialog.Cancel
                            onAccepted: root.ctrl.bakeOffset()
                        }
                    }
                    Note { text: qsTr("Positive: the script comes later. Type a value (step 10 ms, ±10 000) or use ±50. Bake writes it into the script's times; the offset goes back to 0.") }
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "autoSyncSuggest"
                            text: root.ctrl.autoSyncBusy ? qsTr("Listening…") : qsTr("Auto sync")
                            enabled: root.ctrl.hasScript && root.ctrl.videoName !== "" && !root.ctrl.autoSyncBusy
                            onClicked: root.ctrl.suggestAudioSync()
                        }
                        FlowButton {
                            objectName: "autoSyncApply"
                            text: root.ctrl.autoSyncConfidence !== "none"
                                  ? qsTr("Apply %1 ms").arg(root.ctrl.autoSyncOffsetMs >= 0
                                        ? ("+" + root.ctrl.autoSyncOffsetMs)
                                        : String(root.ctrl.autoSyncOffsetMs))
                                  : qsTr("Apply")
                            enabled: root.ctrl.autoSyncCanApply
                            onClicked: root.ctrl.applyAudioSyncSuggestion()
                        }
                    }
                    Note {
                        objectName: "autoSyncNote"
                        visible: root.ctrl.autoSyncNote !== ""
                        text: root.ctrl.autoSyncNote
                    }
                    Note { text: qsTr("Auto sync compares the video's sound (first 10 min) with the stroke and suggests an offset. Apply only changes the offset here; Bake writes it into the script.") }
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
                            enabled: root.ctrl.hasScript && root.ctrl.laneChoices.length > 0
                            model: root.ctrl.laneChoices.map((l) => root.laneTitle(l))
                            currentIndex: Math.max(0, root.ctrl.laneChoices.indexOf(root.ctrl.activeLane))
                            onActivated: (i) => root.ctrl.activeLane = root.ctrl.laneChoices[i]
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
                            enabled: root.ctrl.hasScript && root.ctrl.missingAxisChoices.length > 0
                            model: [qsTr("Choose axis…")].concat(root.ctrl.missingAxisChoices.map((a) => root.axisTitle(a)))
                            currentIndex: 0
                            onActivated: (i) => {
                                if (i <= 0) return
                                const axis = root.ctrl.missingAxisChoices[i - 1]
                                const path = root.ctrl.createCompanionAxis(axis)
                                currentIndex = 0
                                if (path === "") root.projectStatus = root.ctrl.lastError
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
                            enabled: root.ctrl.hasScript
                            model: [qsTr("Recipe"), qsTr("Axes")]
                            currentIndex: root.ctrl.playbackSource === "axes" ? 1 : 0
                            onActivated: (i) => root.ctrl.setPlaybackSource(i === 1 ? "axes" : "recipe")
                        }
                    }
                    FlowButton {
                        objectName: "saveEmotionScript"
                        text: qsTr("Save Emotion Script")
                        icon: "save"
                        enabled: root.ctrl.hasScript
                        onClicked: {
                            const path = root.ctrl.saveAsEmotionScript()
                            if (path === "") root.projectStatus = root.ctrl.lastError
                        }
                    }
                    Note { text: qsTr("Curve picks the lane the main curve shows. + Axis… creates a centred companion file (surge/sway/twist/roll/pitch) beside the script. Drive Recipe/Axes and Save Emotion Script write the .samn.") }
                    Note { objectName: "bakeNote"; visible: root.ctrl.toolNote !== ""; text: root.ctrl.toolNote }
                }
                // Effects (0.7 Player Inspector → Effects and "Playback &
                // Extended-O"): the presets, Intensity and the mapping
                // settings. A change maps the script again at once.
                Card {
                    id: fx
                    title: qsTr("Effects")
                    readonly property var mapping: root.ctrl.mapping
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
                    SliderRow {
                        label: qsTr("Intensity")
                        tip: qsTr("Overall output of vibration and suction, on top of the channel levels. Live.")
                        from: 0; to: 100; value: root.ctrl.output.intensity
                        valueText: root.ctrl.output.intensity + " %"
                        onPicked: (v) => root.ctrl.output.intensity = Math.round(v)
                    }
                    SliderRow {
                        objectName: "fxSmoothing"
                        label: qsTr("Smoothing")
                        help: "playback.smoothing"
                        tip: qsTr("Live curve smoothing toward the next sample (0–1). Default 0.3. Playback feel only.")
                        from: 0; to: 1; value: fx.mapping.smoothing
                        valueText: fx.mapping.smoothing.toFixed(2)
                        onPicked: (v) => fx.mapping.smoothing = Math.round(v * 20) / 20
                    }
                    SliderRow {
                        label: qsTr("Max speed")
                        help: "playback.max-speed"
                        tip: qsTr("The stroke speed (position change per millisecond, 0–100 scale) that gives full strength. Default 0.6. Playback feel only.")
                        from: 0.1; to: 2; value: fx.mapping.maxSpeed
                        valueText: fx.mapping.maxSpeed.toFixed(1)
                        onPicked: (v) => fx.mapping.maxSpeed = Math.round(v * 10) / 10
                    }
                    SliderRow {
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
                    Note { text: qsTr("Soft start and Reduce peaks are under Devices → Output; presets keep them too. The lanes and the device follow at once; the script file is not changed.") }
                }
                // Contact vibration and the strength presets (0.7 Player
                // "Contact vibration" block and the Strength choice).
                Card {
                    id: contact
                    title: qsTr("Contact vibration & strength")
                    visible: (root.ctrl.contactControls && root.ctrl.playbackSource !== "axes") || root.ctrl.strengthPresets.length > 0
                    readonly property var mapping: root.ctrl.mapping
                    readonly property var probe: root.ctrl.contactFeelProbe
                    readonly property bool contactLive: root.ctrl.contactControls && root.ctrl.playbackSource !== "axes"
                    FlowButton {
                        objectName: "contactOff"
                        visible: contact.contactLive
                        text: qsTr("Contact vibration off"); checked: contact.mapping.contactOff
                        onClicked: contact.mapping.contactOff = !contact.mapping.contactOff
                        HoverHandler { id: offHover }
                        ToolTipText { shown: offHover.hovered; text: qsTr("Turns off the contact vibration the script carries, without changing the file.") }
                    }
                    SliderRow {
                        visible: contact.contactLive
                        label: qsTr("Strength")
                        help: "playback.contact-strength"
                        tip: qsTr("Live scaling of the contact vibration without rewriting the file. 1 = as created, 0 = off, up to 2 = stronger.")
                        from: 0; to: 2; value: contact.mapping.contactIntensity
                        valueText: contact.mapping.contactIntensity === 0 ? qsTr("0 (off)") : contact.mapping.contactIntensity.toFixed(2)
                        onPicked: (v) => contact.mapping.contactIntensity = Math.round(v * 20) / 20
                    }
                    SliderRow {
                        visible: contact.contactLive
                        active: root.ctrl.contactKnobsLive
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
                            enabled: root.ctrl.contactKnobsLive; opacity: enabled ? 1 : 0.45
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
                        enabled: root.ctrl.hasScript && root.ctrl.contactKnobsLive
                        onClicked: root.ctrl.saveContactToScript()
                        HoverHandler { id: saveContactHover }
                        ToolTipText { shown: saveContactHover.hovered; text: qsTr("Write Contact on/off, Sensitivity and Curve into the Emotion Script recipe.") }
                    }
                    Note {
                        objectName: "contactSaveNote"
                        visible: root.ctrl.toolNote !== "" && root.ctrl.toolNote.indexOf("Contact settings") === 0
                        text: root.ctrl.toolNote
                    }
                    Note {
                        visible: contact.contactLive && !root.ctrl.contactKnobsLive
                        text: qsTr("Sensitivity and curve act when the script plays from its recipe with contact vibration on.")
                    }
                    Row {
                        visible: root.ctrl.strengthPresets.length > 0
                        spacing: 8
                        Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; width: 78; text: qsTr("Preset"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                               HoverHandler { id: strengthHover }
                               ToolTipText { shown: strengthHover.hovered; text: qsTr("Soft / normal / strong feel of this Emotion Script. Saved in the script.") } }
                        FlowCombo {
                            objectName: "strengthPreset"
                            width: contact.width - 24 - 86
                            model: ["—"].concat(root.ctrl.strengthPresets)
                            currentIndex: root.ctrl.activeStrength === "" ? 0 : 1 + root.ctrl.strengthPresets.indexOf(root.ctrl.activeStrength)
                            onActivated: (i) => root.ctrl.activeStrength = i === 0 ? "" : root.ctrl.strengthPresets[i - 1]
                        }
                    }
                }
                // Extended-O (0.7 "Playback & Extended-O" and the transport's
                // Extended-O button; key E).
                Card {
                    title: qsTr("Extended-O")
                    Flow {
                        width: parent.width; spacing: 8
                        FlowButton {
                            objectName: "eoTrigger"
                            text: root.ctrl.output.extendedOActive ? qsTr("Running…") : qsTr("Extended-O")
                            primary: true
                            enabled: root.ctrl.output.canTriggerExtendedO && !root.ctrl.output.extendedOActive
                            onClicked: root.ctrl.output.triggerExtendedO()
                            HoverHandler { id: eoHover }
                            ToolTipText { shown: eoHover.hovered; text: qsTr("Lowers vibration and suction to the minimum for the hold time, then brings them back. The curve keeps its rhythm. While playing (E).") }
                        }
                        FlowButton {
                            text: qsTr("Enabled"); checked: root.ctrl.output.extendedOEnabled
                            onClicked: root.ctrl.output.extendedOEnabled = !root.ctrl.output.extendedOEnabled
                        }
                    }
                    SliderRow {
                        label: qsTr("Minimum")
                        tip: qsTr("Height of the curve while held, as a share of the script (0 % = off). Default 10 %.")
                        from: 0; to: 1; value: root.ctrl.output.extendedOMin
                        valueText: root.ctrl.output.extendedOMin === 0 ? qsTr("0 % (off)") : Math.round(root.ctrl.output.extendedOMin * 100) + " %"
                        onPicked: (v) => root.ctrl.output.extendedOMin = Math.round(v * 20) / 20
                    }
                    SliderRow {
                        label: qsTr("Hold")
                        help: "playback.hold-s"
                        tip: qsTr("How long the minimum is held. Default 10 s.")
                        from: 1; to: 60; value: root.ctrl.output.extendedOHoldS
                        valueText: qsTr("%1 s").arg(Math.round(root.ctrl.output.extendedOHoldS))
                        onPicked: (v) => root.ctrl.output.extendedOHoldS = Math.round(v)
                    }
                    SliderRow {
                        label: qsTr("Restore")
                        help: "playback.restore-ms"
                        tip: qsTr("Ramp time down to the minimum and back up again. 0 switches at once. Default 500 ms.")
                        from: 0; to: 3000; value: root.ctrl.output.extendedORestoreMs
                        valueText: root.ctrl.output.extendedORestoreMs === 0 ? qsTr("At once") : qsTr("%1 ms").arg(root.ctrl.output.extendedORestoreMs)
                        onPicked: (v) => root.ctrl.output.extendedORestoreMs = Math.round(v / 100) * 100
                    }
                    FlowButton {
                        text: qsTr("Auto in marked range"); checked: root.ctrl.autoExtendedO
                        onClicked: root.ctrl.autoExtendedO = !root.ctrl.autoExtendedO
                        HoverHandler { id: autoEoHover }
                        ToolTipText { shown: autoEoHover.hovered; text: qsTr("Triggers Extended-O once when playback reaches the marked range of the heatmap.") }
                    }
                    Note {
                        text: !root.ctrl.output.extendedOEnabled ? qsTr("Switched off.")
                              : root.ctrl.autoExtendedO && !root.ctrl.hasMarker ? qsTr("Mark a range on the heatmap for the automatic trigger.")
                              : qsTr("Available while playing. A press while it runs is ignored.")
                    }
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
                    help: "pb-marker-hint"
                    Row {
                        spacing: 8
                        FlowButton {
                            text: qsTr("Audio level lane"); checked: root.ctrl.audioLane
                            onClicked: root.ctrl.audioLane = !root.ctrl.audioLane
                        }
                        FlowButton {
                            objectName: "showScenes"
                            text: qsTr("Scene markers"); checked: root.ctrl.showScenes
                            onClicked: root.ctrl.showScenes = !root.ctrl.showScenes
                        }
                    }
                    Row {
                        spacing: 8
                        FlowButton {
                            text: qsTr("0–100 on video"); checked: root.ctrl.posOverlay
                            onClicked: root.ctrl.posOverlay = !root.ctrl.posOverlay
                        }
                    }
                    // 0.7 "Show contact areas" with its hint, when the script stores marks.
                    FlowButton {
                        objectName: "showContactMarks"
                        visible: root.ctrl.contactMarks.hint !== undefined
                        text: qsTr("Show contact areas"); checked: root.ctrl.showContactMarks
                        onClicked: root.ctrl.showContactMarks = !root.ctrl.showContactMarks
                    }
                    Text {
                        objectName: "contactMarksHint"
                        textFormat: Text.PlainText
                        visible: root.ctrl.contactMarks.hint !== undefined
                        width: parent.width; wrapMode: Text.WordWrap
                        text: root.ctrl.contactMarks.hint !== undefined ? root.ctrl.contactMarks.hint : ""
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                    }
                    FlowButton {
                        objectName: "videoPlayAutostart"
                        text: qsTr("Video play starts device")
                        checked: root.ctrl.videoPlayAutostart
                        onClicked: root.ctrl.videoPlayAutostart = !root.ctrl.videoPlayAutostart
                    }
                    Note { text: qsTr("When off, the film can play without moving the toy. Script Play (no video) still starts the device.") }
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "pb-mock"
                            text: qsTr("Mock (no device)")
                            checked: root.ctrl.playbackMock
                            onClicked: root.ctrl.playbackMock = !root.ctrl.playbackMock
                        }
                        HelpChip { key: "pb-mock"; anchors.verticalCenter: parent.verticalCenter }
                    }
                    Note {
                        text: qsTr("On (default): Play connects a simulated device when nothing is connected. Off: connect a real toy in Devices first.")
                    }
                    // MT-Debug tip/partner path (0.7 Show tip path).
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "showTrajectory"
                            visible: root.ctrl.hasTrajectory
                            text: qsTr("Show tip path"); checked: root.ctrl.trajectoryOverlay
                            onClicked: root.ctrl.trajectoryOverlay = !root.ctrl.trajectoryOverlay
                        }
                        HelpChip {
                            visible: root.ctrl.hasTrajectory
                            anchors.verticalCenter: parent.verticalCenter
                            key: "pb-trajectory-toggle"
                        }
                    }
                    Text {
                        objectName: "trajectoryHint"
                        textFormat: Text.PlainText
                        visible: root.ctrl.hasScript && !root.ctrl.hasTrajectory
                        width: parent.width; wrapMode: Text.WordWrap
                        text: qsTr("This script has no recorded trajectory — regenerate with contact marks (Record tip path).")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
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
                    Note { text: qsTr("Heatmap: click = seek, drag = mark. Wheel on the heatmap zooms. Curve edit below uses the marked range.") }
                }
                // Curve edit on the heatmap selection (0.7 Cap · Scale · Delete · Invert).
                MessageDialog {
                    id: invertAllDialog
                    objectName: "curveInvertAllDialog"
                    title: qsTr("Invert the curve?")
                    text: qsTr("Invert the whole lane? There is no undo here — open the Editor if you need one.")
                    buttons: MessageDialog.Ok | MessageDialog.Cancel
                    onAccepted: root.ctrl.invertAll()
                }
                Card {
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
                            from: 150; to: 800; value: root.ctrl.capIntensity
                            onMoved: (v) => root.ctrl.capIntensity = Math.round(v / 25) * 25
                        }
                        Text {
                            textFormat: Text.PlainText
                            anchors.verticalCenter: parent.verticalCenter
                            text: Math.round(root.ctrl.capIntensity)
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                    FlowButton {
                        objectName: "curveCap"
                        text: qsTr("Speed-cap selection")
                        enabled: root.ctrl.hasScript && root.ctrl.hasMarker
                        onClicked: root.ctrl.capSelection()
                    }
                    FlowButton {
                        objectName: "curveSoftEdges"
                        text: qsTr("Soft edges"); checked: root.ctrl.scaleSoftEdges
                        onClicked: root.ctrl.scaleSoftEdges = !root.ctrl.scaleSoftEdges
                    }
                    Row {
                        spacing: 8
                        FlowButton {
                            objectName: "curveSofter"
                            text: qsTr("Softer")
                            enabled: root.ctrl.hasScript && root.ctrl.hasMarker
                            onClicked: root.ctrl.scaleSelection(0.8)
                        }
                        FlowButton {
                            objectName: "curveStronger"
                            text: qsTr("Stronger")
                            enabled: root.ctrl.hasScript && root.ctrl.hasMarker
                            onClicked: root.ctrl.scaleSelection(1.25)
                        }
                        FlowButton {
                            objectName: "curveDelete"
                            text: qsTr("Delete range")
                            enabled: root.ctrl.hasScript && root.ctrl.hasMarker
                            onClicked: root.ctrl.deleteSelection()
                        }
                        FlowButton {
                            objectName: "curveInvert"
                            text: root.ctrl.hasMarker ? qsTr("Invert range") : qsTr("Invert all")
                            enabled: root.ctrl.hasScript
                            onClicked: {
                                if (root.ctrl.hasMarker) root.ctrl.invertSelection()
                                else invertAllDialog.open()
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
                            width: curveEdit.width - 24 - 78 - 48
                            from: 0; to: 60; value: root.ctrl.fpsSnap
                            onMoved: (v) => root.ctrl.fpsSnap = Math.round(v)
                        }
                        Text {
                            textFormat: Text.PlainText
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.ctrl.fpsSnap > 0 ? qsTr("%1").arg(Math.round(root.ctrl.fpsSnap)) : qsTr("off")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                    Note { text: qsTr("Seeks snap to frame boundaries when FPS-Snap is on (0 = off).") }
                    Note { visible: !root.ctrl.hasMarker; text: qsTr("Mark a range for Cap / Scale / Delete. Invert all works without one.") }
                    Note { objectName: "curveEditNote"; visible: root.ctrl.toolNote !== ""; text: root.ctrl.toolNote }
                    FlowButton {
                        objectName: "editScriptFromCurve"
                        text: qsTr("More editing in the Editor…"); icon: "edit"
                        enabled: root.ctrl.hasScript
                        onClicked: root.editRequested(false)
                    }
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
                    title: qsTr("Full curve editing")
                    Note { text: qsTr("Point drag, undo and scenes are in the Generator (the Editor). Cap / Softer / Stronger / Delete / Invert on a heatmap mark are under Curve edit above.") }
                    FlowButton { objectName: "editScript"; text: qsTr("Edit this script"); icon: "edit"; enabled: root.ctrl.hasScript; onClicked: root.editRequested(false) }
                }
            }
        }
    }
}
