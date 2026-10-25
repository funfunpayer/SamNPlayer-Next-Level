import QtQuick
import QtQuick.Shapes
import SamN

// 0.7's dual intensity ring beside the training clip (training.js
// renderDualIntensityRing, style.css .tr-ring-*): one ring, both channels
// drawn full. The level is in the motion, not in the fill: vibration pulses
// its track outward (amplitude level × 0.14, 0.55 s), suction contracts and
// widens the whole ring (level × 0.18, 1.05 s); each track brightens with
// its level (0.45 + 0.55 · level, idle 0.28). A halo breathes (3.6 s, 1.8 s
// live), the ring tilts (5.5 s), a gloss arc sweeps round (4.5 s), the two
// percentages tick (1.2 s). The Flow colours of the Training replace 0.7's
// lilac and teal.
Item {
    id: root
    property real vibration: 0
    property real suction: 0
    // A session runs: the channels with a level move.
    property bool running: false
    readonly property bool vibPulsing: running && vibration > 0
    readonly property bool sucPulsing: running && suction > 0
    readonly property bool live: vibPulsing || sucPulsing
    readonly property real r: 52
    implicitWidth: 128 + legend.implicitWidth + 18
    implicitHeight: 128

    function trackOpacity(level) { return level > 0.01 ? 0.45 + 0.55 * Math.min(1, level) : 0.28 }

    Item {
        id: wrap
        objectName: "dualRingWrap"
        width: 128; height: 128
        anchors.verticalCenter: parent.verticalCenter
        // Suction breathes the whole ring: together, then apart.
        property real sucAmp: Math.min(1, Math.max(0, root.suction)) * 0.18
        SequentialAnimation on scale {
            running: root.sucPulsing && !Theme.reducedMotion
            loops: Animation.Infinite
            onRunningChanged: if (!running) wrap.scale = 1
            NumberAnimation { to: 1 - wrap.sucAmp; duration: 1050 * 0.35; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1 + wrap.sucAmp * 0.6; duration: 1050 * 0.35; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 1050 * 0.30; easing.type: Easing.InOutSine }
        }

        Rectangle {
            id: halo
            anchors.centerIn: parent
            width: 128; height: 128; radius: 64
            color: "transparent"
            border.width: 10
            border.color: Qt.rgba(Theme.trainVibration.r, Theme.trainVibration.g, Theme.trainVibration.b, 0.18)
            opacity: root.live ? 1 : 0.55
            readonly property int period: root.live ? 1800 : 3600
            SequentialAnimation on scale {
                running: !Theme.reducedMotion; loops: Animation.Infinite
                NumberAnimation { from: 0.96; to: 1.04; duration: halo.period / 2; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.04; to: 0.96; duration: halo.period / 2; easing.type: Easing.InOutSine }
            }
        }

        Item {
            id: ring
            anchors.fill: parent
            transform: Rotation {
                id: tilt
                origin.x: 64; origin.y: 64
                axis { x: 1; y: -1.3; z: 0 }
                SequentialAnimation on angle {
                    running: !Theme.reducedMotion; loops: Animation.Infinite
                    NumberAnimation { from: 7; to: -7; duration: 2750; easing.type: Easing.InOutSine }
                    NumberAnimation { from: -7; to: 7; duration: 2750; easing.type: Easing.InOutSine }
                }
            }
            Shape {
                anchors.fill: parent
                layer.enabled: true; layer.samples: 4
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.04); strokeWidth: 14; fillColor: "transparent"
                    PathAngleArc { centerX: 64; centerY: 64; radiusX: root.r + 8; radiusY: root.r + 8; startAngle: 0; sweepAngle: 360 }
                }
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.08); strokeWidth: 12; fillColor: "transparent"
                    PathAngleArc { centerX: 64; centerY: 64; radiusX: root.r; radiusY: root.r; startAngle: 0; sweepAngle: 360 }
                }
            }
            // Vibration: its own group, pulsing outward.
            Shape {
                id: vibTrack
                objectName: "dualRingVibration"
                anchors.fill: parent
                layer.enabled: true; layer.samples: 4
                opacity: root.trackOpacity(root.vibration)
                property real amp: Math.min(1, Math.max(0, root.vibration)) * 0.14
                SequentialAnimation on scale {
                    running: root.vibPulsing && !Theme.reducedMotion
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) vibTrack.scale = 1
                    NumberAnimation { to: 1 + vibTrack.amp; duration: 275; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 275; easing.type: Easing.InOutSine }
                }
                ShapePath {
                    strokeWidth: 12; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    strokeColor: Theme.trainVibration
                    PathAngleArc { centerX: 64; centerY: 64; radiusX: root.r; radiusY: root.r; startAngle: -90; sweepAngle: 360 }
                }
            }
            Shape {
                objectName: "dualRingSuction"
                anchors.fill: parent
                layer.enabled: true; layer.samples: 4
                opacity: root.trackOpacity(root.suction) * 0.88
                ShapePath {
                    strokeWidth: 6; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    strokeColor: Theme.trainSuction
                    PathAngleArc { centerX: 64; centerY: 64; radiusX: root.r; radiusY: root.r; startAngle: -90; sweepAngle: 360 }
                }
            }
            // The gloss: a tenth of the ring, sweeping round.
            Shape {
                anchors.fill: parent
                layer.enabled: true; layer.samples: 4
                RotationAnimator on rotation {
                    from: 0; to: 360; duration: 4500; loops: Animation.Infinite
                    running: !Theme.reducedMotion
                }
                ShapePath {
                    strokeWidth: 2.5; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    strokeColor: Qt.rgba(1, 1, 1, 0.62)
                    PathAngleArc { centerX: 64; centerY: 64; radiusX: root.r; radiusY: root.r; startAngle: -108; sweepAngle: 36 }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            spacing: 2
            SequentialAnimation on scale {
                running: root.live && !Theme.reducedMotion
                loops: Animation.Infinite
                onRunningChanged: if (!running) parent.scale = 1
                NumberAnimation { to: 1.04; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: 600; easing.type: Easing.InOutSine }
            }
            Text {
                objectName: "dualRingVibrationValue"
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: Math.round(Math.min(1, Math.max(0, root.vibration)) * 100) + "%"
                color: Theme.trainVibration; font.family: Theme.font; font.pixelSize: 15; font.bold: true
            }
            Text {
                objectName: "dualRingSuctionValue"
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: Math.round(Math.min(1, Math.max(0, root.suction)) * 100) + "%"
                color: Theme.trainSuction; font.family: Theme.font; font.pixelSize: 15; font.bold: true
            }
        }
    }

    Column {
        id: legend
        anchors.left: wrap.right; anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        Repeater {
            model: [{ name: qsTr("Vibration"), tint: Theme.trainVibration }, { name: qsTr("Suction"), tint: Theme.trainSuction }]
            delegate: Row {
                required property var modelData
                spacing: 7
                Rectangle { width: 11; height: 11; radius: 5.5; color: modelData.tint; anchors.verticalCenter: parent.verticalCenter }
                Text { textFormat: Text.PlainText; text: modelData.name; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
            }
        }
    }
}
