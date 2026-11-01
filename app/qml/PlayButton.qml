import QtQuick
import SamN
import QtQuick.Shapes

// The round glowing Play button (the app symbol): cyan→orange silk ring,
// soft halo, calm idle drift that quickens while playing (0.8 #550).
Item {
    id: root
    property bool playing: false
    property bool motion: Qt.application.state === Qt.ApplicationActive && !Theme.reducedMotion
    signal clicked()
    width: 72; height: 72

    // Breathing halo — cyan left / orange right (flow-play::after).
    Item {
        id: halo
        anchors.centerIn: parent
        width: root.width * (area.containsMouse ? 1.58 : 1.48); height: width
        z: -1
        Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        property real breathe: 0.85
        opacity: breathe * (area.containsMouse ? 1.08 : 1.0)
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: halo.width * 0.32; centerY: halo.height * 0.42; centerRadius: halo.width * 0.44
                    focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: Qt.rgba(63 / 255, 208 / 255, 1, 0.40) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathAngleArc { centerX: halo.width / 2; centerY: halo.height / 2; radiusX: halo.width / 2; radiusY: halo.height / 2; sweepAngle: 360 }
            }
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: halo.width * 0.7; centerY: halo.height * 0.6; centerRadius: halo.width * 0.42
                    focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: Qt.rgba(1, 138 / 255, 35 / 255, 0.32) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathAngleArc { centerX: halo.width / 2; centerY: halo.height / 2; radiusX: halo.width / 2; radiusY: halo.height / 2; sweepAngle: 360 }
            }
        }
        readonly property int period: root.playing ? 2400 : 4500
        SequentialAnimation on breathe {
            running: root.motion
            loops: Animation.Infinite
            NumberAnimation { to: root.playing ? 0.55 : 0.50; duration: halo.period / 2; easing.type: Easing.InOutSine }
            NumberAnimation { to: root.playing ? 1.0 : 0.90; duration: halo.period / 2; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on scale {
            running: root.motion
            loops: Animation.Infinite
            NumberAnimation { to: root.playing ? 1.06 : 1.04; duration: halo.period / 2; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: halo.period / 2; easing.type: Easing.InOutSine }
        }
    }

    // Conical silk disc; the face covers the centre so a soft ring remains.
    // Idle: slow reverse drift. Playing: faster forward turn.
    Shape {
        id: ring
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        opacity: root.playing ? 1.0 : 0.90
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: ConicalGradient {
                centerX: ring.width / 2; centerY: ring.height / 2; angle: 0
                GradientStop { position: 0.0; color: Theme.cyan }
                GradientStop { position: 0.22; color: "#e9fbff" }
                GradientStop { position: 0.45; color: Theme.blue }
                GradientStop { position: 0.72; color: Theme.orange }
                GradientStop { position: 1.0; color: Theme.cyan }
            }
            PathAngleArc {
                centerX: ring.width / 2; centerY: ring.height / 2
                radiusX: ring.width / 2; radiusY: ring.height / 2
                startAngle: 0; sweepAngle: 360
            }
        }
        RotationAnimator on rotation {
            from: 0; to: 360
            duration: root.playing ? 2400 : Theme.animAmbient
            loops: Animation.Infinite
            running: root.motion
            direction: root.playing ? RotationAnimator.Clockwise : RotationAnimator.Counterclockwise
        }
    }

    Rectangle {
        id: face
        anchors.centerIn: parent
        width: parent.width - 10; height: width; radius: width / 2
        // Lighter navy face — a near-black disc read as a muddy oval on the
        // empty home (Owner 26 Oct). Keep depth without a black hole.
        gradient: Gradient {
            GradientStop { position: 0; color: "#124a78" }
            GradientStop { position: 0.55; color: "#0a2c4c" }
            GradientStop { position: 1; color: "#061828" }
        }
        border.width: 1
        border.color: Qt.rgba(90 / 255, 190 / 255, 255 / 255, area.containsMouse ? 0.55 : 0.34)
        scale: area.pressed ? 0.94 : (area.containsMouse ? 1.05 : 1.0)
        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        // Soft glyph glow (CSS drop-shadow stand-in).
        Canvas {
            anchors.centerIn: parent
            width: parent.width * 0.42; height: width
            opacity: 0.45
            scale: 1.14
            property bool playing: root.playing
            onPlayingChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d");
                c.reset();
                c.fillStyle = "#3fd0ff";
                if (playing) {
                    c.fillRect(width * 0.16, -1, width * 0.26, height + 2);
                    c.fillRect(width * 0.58, -1, width * 0.26, height + 2);
                } else {
                    c.beginPath();
                    c.moveTo(width * 0.18, -1);
                    c.lineTo(width + 2, height / 2);
                    c.lineTo(width * 0.18, height + 1);
                    c.closePath();
                    c.fill();
                }
            }
        }
        Canvas {
            id: glyph
            anchors.centerIn: parent
            width: parent.width * 0.42; height: width
            property bool playing: root.playing
            onPlayingChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d");
                c.reset();
                c.fillStyle = "#3fd0ff";
                if (playing) {
                    c.fillRect(width * 0.18, 0, width * 0.22, height);
                    c.fillRect(width * 0.6, 0, width * 0.22, height);
                } else {
                    c.beginPath();
                    c.moveTo(width * 0.2, 0);
                    c.lineTo(width, height / 2);
                    c.lineTo(width * 0.2, height);
                    c.closePath();
                    c.fill();
                }
            }
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
