import QtQuick
import QtQuick.Shapes
import SamN

// One glowing gauge of the Training stage (0.7 training_rings.js, the Owner's target):
// a faint track, the level as a glowing arc from the top, the name, the percentage and
// a small wave mark. The arc is redrawn when the level changes (about 20 times a second
// while a session runs), not per frame.
Item {
    id: root
    property string label
    property real level: 0        // 0-1
    property color tint: Theme.trainVibration
    property bool live: false      // a session runs: the ring is bright
    implicitWidth: 236; implicitHeight: 236
    readonly property real radius_: Math.min(width, height) / 2 - 26
    readonly property real sweep: Math.max(0, Math.min(1, level)) * 360

    // Soft light behind the whole gauge.
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 + 56; height: width; radius: width / 2
        color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, root.live ? 0.10 : 0.05)
    }
    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        // The track: a full ring, faint.
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.20); strokeWidth: 6; fillColor: "transparent"
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // Glow: three wider, fainter strokes under the arc.
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.08); strokeWidth: 30; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.16); strokeWidth: 20; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.35); strokeWidth: 12; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        // The arc itself, with a bright core.
        ShapePath {
            strokeColor: root.tint; strokeWidth: 7; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
        ShapePath {
            strokeColor: Qt.lighter(root.tint, 1.7); strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
            PathAngleArc { centerX: root.width / 2; centerY: root.height / 2; radiusX: root.radius_; radiusY: root.radius_; startAngle: -90; sweepAngle: root.sweep }
        }
    }
    // The dark inside.
    Rectangle {
        anchors.centerIn: parent
        width: root.radius_ * 2 - 14; height: width; radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#06182c" }
            GradientStop { position: 1; color: "#020a16" }
        }
        border.color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.25); border.width: 1
    }
    Column {
        anchors.centerIn: parent
        spacing: 2
        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label.toUpperCase()
            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14; font.letterSpacing: 1.2
        }
        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: Math.round(root.level * 100) + "%"
            color: "#ffffff"; font.family: Theme.font; font.pixelSize: 42; font.weight: Font.DemiBold
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "waveform"; color: root.tint; width: 30; height: 20
            opacity: root.live ? 1 : 0.5
        }
    }
}
