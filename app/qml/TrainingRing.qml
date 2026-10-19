import QtQuick
import QtQuick.Shapes
import SamN

// One glowing gauge of the Training stage (0.8 #550 theme_flow.css rings stage,
// docs/visual-target/training.png): a lit shell ring that glows even at rest,
// a halo that grows with the level, the level as a bright arc from the top,
// and the name, a large percentage and a small wave mark in the dark centre.
// The arc is redrawn when the level changes (about 20 times a second while a
// session runs), not per frame.
Item {
    id: root
    property string label
    property real level: 0        // 0-1
    property color tint: Theme.trainVibration
    property bool live: false      // a session runs: the shell dims, the arc leads
    implicitWidth: 236; implicitHeight: 236
    readonly property real radius_: Math.min(width, height) / 2 - 24
    readonly property real sweep: Math.max(0, Math.min(1, level)) * 360
    readonly property real cx: width / 2
    readonly property real cy: height / 2
    function tinted(a) { return Qt.rgba(root.tint.r, root.tint.g, root.tint.b, a) }

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        // Halo: wide and faint, stronger with the level (.08 + level × .38).
        ShapePath {
            strokeColor: root.tinted((0.08 + root.level * 0.38) * 0.35); strokeWidth: 34; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // Shell: the whole ring in the channel colour, lit at rest (.55),
        // quieter while a session runs (.22) so the arc reads.
        ShapePath {
            strokeColor: root.tinted((root.live ? 0.22 : 0.55) * 0.35); strokeWidth: 20; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        ShapePath {
            strokeColor: root.tinted(root.live ? 0.22 : 0.55); strokeWidth: 10; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // Track under the arc.
        ShapePath {
            strokeColor: Qt.rgba(160 / 255, 210 / 255, 240 / 255, 0.10); strokeWidth: 10; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // The level arc with its glow and a bright core.
        ShapePath {
            strokeColor: root.tinted(0.18); strokeWidth: 26; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        ShapePath {
            strokeColor: root.tint; strokeWidth: 9; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        ShapePath {
            strokeColor: Qt.lighter(root.tint, 1.7); strokeWidth: 2.5; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
    }
    // The dark centre with a soft glow at its rim.
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 - 22; height: width; radius: width / 2
        color: "transparent"
        border.color: root.tinted(0.30); border.width: 6
        opacity: 0.6
    }
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 - 24; height: width; radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.rgba(4 / 255, 16 / 255, 32 / 255, 0.96) }
            GradientStop { position: 1; color: Qt.rgba(2 / 255, 10 / 255, 22 / 255, 0.96) }
        }
    }
    Column {
        anchors.centerIn: parent
        spacing: 0
        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label.toUpperCase()
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13; font.letterSpacing: 1.6
        }
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: pct.implicitWidth; height: pct.implicitHeight
            Text {
                id: pct
                textFormat: Text.PlainText
                text: Math.round(root.level * 100) + "%"
                color: "#f4fbff"; font.family: Theme.font; font.pixelSize: Math.max(28, root.radius_ * 0.52); font.weight: Font.Bold
            }
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "waveform"; color: root.tint; width: 34; height: 20
            opacity: root.live ? 1 : 0.75
        }
    }
}
