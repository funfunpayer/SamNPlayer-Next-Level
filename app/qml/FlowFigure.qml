import QtQuick
import QtQuick.Shapes
import SamN

// The abstract Flow figure for the Training stage (D-152; it replaces 0.7's
// pixel clip): a glowing infinity loop that opens with the level, and a light
// that runs along it faster the higher the level is. The tint warms from calm
// cyan to orange and red like the session timeline.
Item {
    id: root
    // 0..1, what the device gets now (or the idle demo).
    property real level: 0
    property color tint: Theme.cyan
    // Off while hidden: no paint, no timer.
    property bool running: visible

    readonly property real a: Math.min(width * 0.42, height * 0.95)
    // How far the loop opens: flat at 0, round at 1.
    readonly property real open: 0.35 + Math.max(0, Math.min(1, level)) * 0.85
    property real phase: 0

    function pointAt(t) {
        const s = Math.sin(t), c = Math.cos(t), d = 1 + s * s
        return Qt.point(width / 2 + a * c / d, height / 2 + a * s * c / d * open * 1.6)
    }
    readonly property var path: {
        const pts = []
        for (let i = 0; i <= 160; ++i) pts.push(pointAt(i / 160 * Math.PI * 2))
        return pts
    }

    Timer {
        interval: 16; repeat: true; running: root.running
        onTriggered: root.phase = (root.phase + 0.012 + root.level * 0.05) % (Math.PI * 2)
    }

    Shape {
        anchors.fill: parent
        layer.enabled: true; layer.samples: 4
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.10 + root.level * 0.10)
            strokeWidth: 26; fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.path }
        }
        ShapePath {
            strokeColor: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.30 + root.level * 0.25)
            strokeWidth: 10; fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.path }
        }
        ShapePath {
            strokeColor: Qt.lighter(root.tint, 1.35)
            strokeWidth: 3; fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.path }
        }
    }

    // The running light and its glow.
    Repeater {
        model: 3
        delegate: Rectangle {
            required property int index
            readonly property point p: root.pointAt(root.phase - index * 0.09)
            readonly property real size: (22 - index * 6) * (0.8 + root.level * 0.6)
            x: p.x - size / 2; y: p.y - size / 2
            width: size; height: size; radius: size / 2
            color: index === 0 ? "#ffffff" : root.tint
            opacity: index === 0 ? 0.95 : 0.35 - index * 0.1
        }
    }
    Shape {
        id: glow
        readonly property point p: root.pointAt(root.phase)
        readonly property real r: 32 * (0.7 + root.level * 0.8)
        x: p.x - r; y: p.y - r; width: 2 * r; height: 2 * r
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.r; centerY: glow.r; focalX: centerX; focalY: centerY
                centerRadius: glow.r
                GradientStop { position: 0; color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.45) }
                GradientStop { position: 1; color: "transparent" }
            }
            PathAngleArc { centerX: glow.r; centerY: glow.r; radiusX: glow.r; radiusY: glow.r; startAngle: 0; sweepAngle: 360 }
        }
    }
}
