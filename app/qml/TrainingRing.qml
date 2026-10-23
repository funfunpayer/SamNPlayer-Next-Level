import QtQuick
import QtQuick.Shapes
import SamN

// One glowing gauge of the Training stage (0.8 #550 / docs/visual-target/training.png):
// soft lit shell at rest, halo that grows with the level, bright level arc from
// the top, name + large % + wave in the dark centre. Kept clear and readable —
// no muddy stacked glows.
Item {
    id: root
    property string label
    property real level: 0        // 0-1
    property color tint: Theme.trainVibration
    property bool live: false      // a session runs: the shell dims, the arc leads
    property bool motion: Qt.application.state === Qt.ApplicationActive
    implicitWidth: 236; implicitHeight: 236
    readonly property real radius_: Math.min(width, height) / 2 - 22
    // Soft follow so the arc eases between ~20 Hz level ticks.
    property real shown: level
    Behavior on shown { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    readonly property real sweep: Math.max(0, Math.min(1, shown)) * 360
    readonly property real cx: width / 2
    readonly property real cy: height / 2
    function tinted(a) { return Qt.rgba(root.tint.r, root.tint.g, root.tint.b, a) }

    // Gentle halo breathe only while live (0.8 flow-tr-halo-breathe ~3.2 s).
    property real haloMul: 1.0
    SequentialAnimation on haloMul {
        running: root.live && root.motion
        loops: Animation.Infinite
        NumberAnimation { to: 1.22; duration: 1600; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0.90; duration: 1600; easing.type: Easing.InOutSine }
        onStopped: root.haloMul = 1.0
    }

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        // Outer halo — soft bloom, stronger with level (mockup glow).
        ShapePath {
            strokeColor: root.tinted((0.06 + root.shown * 0.32) * root.haloMul)
            strokeWidth: 32; fillColor: "transparent"
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: 0; sweepAngle: 360
            }
        }
        // Soft shell rim — readable at rest, quieter when live so the arc leads.
        ShapePath {
            strokeColor: root.tinted(root.live ? 0.16 : 0.42)
            strokeWidth: 14; fillColor: "transparent"
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: 0; sweepAngle: 360
            }
        }
        ShapePath {
            strokeColor: root.tinted(root.live ? 0.28 : 0.70)
            strokeWidth: 8; fillColor: "transparent"
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: 0; sweepAngle: 360
            }
        }
        // Quiet track under the arc.
        ShapePath {
            strokeColor: Qt.rgba(160 / 255, 210 / 255, 240 / 255, 0.12)
            strokeWidth: 9; fillColor: "transparent"
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: 0; sweepAngle: 360
            }
        }
        // Level arc: soft outer glow + bright core (the reading).
        ShapePath {
            strokeColor: root.tinted(0.28); strokeWidth: 22; fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: -90; sweepAngle: root.sweep
            }
        }
        ShapePath {
            strokeColor: root.tint; strokeWidth: 8; fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: -90; sweepAngle: root.sweep
            }
        }
        ShapePath {
            strokeColor: Qt.lighter(root.tint, 1.65); strokeWidth: 2.2; fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.cx; centerY: root.cy
                radiusX: root.radius_; radiusY: root.radius_
                startAngle: -90; sweepAngle: root.sweep
            }
        }
    }

    // Dark centre disc with a tinted rim (mockup gauge face).
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 - 20; height: width; radius: width / 2
        color: "transparent"
        border.color: root.tinted(0.38); border.width: 5
        opacity: 0.7
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 - 22; height: width; radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(4 / 255, 16 / 255, 32 / 255, 0.97) }
            GradientStop { position: 1; color: Qt.rgba(2 / 255, 10 / 255, 22 / 255, 0.97) }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 2
        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label.toUpperCase()
            color: root.tinted(0.85)
            font.family: Theme.fontSoft; font.pixelSize: 12; font.letterSpacing: 1.8
            font.weight: Font.DemiBold
        }
        Text {
            id: pct
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.round(root.shown * 100) + "%"
            color: "#f4fbff"
            font.family: Theme.font
            font.pixelSize: Math.max(30, root.radius_ * 0.55)
            font.weight: Font.Bold
        }
        // Soft stand-in for CSS text-shadow on the % (mockup glow).
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "waveform"; color: root.tint; width: 36; height: 18
            opacity: root.live ? 0.95 : 0.70
        }
    }
}
