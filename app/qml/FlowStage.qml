import QtQuick
import QtQuick.Shapes
import SamN

// The Flow stage (0.7 flow_fx.js): a breathing halo, two light ribbons and
// the floating play mark. Calm while idle, faster while playing, still when
// paused. Decorative only.
Item {
    id: root
    property string state_: "idle"   // "idle" | "playing" | "paused"
    readonly property bool moving: state_ !== "paused" && visible
    readonly property real pace: state_ === "playing" ? 0.55 : 1.0
    implicitWidth: 420; implicitHeight: 236

    // Halo: radial glow behind everything (breathes).
    Shape {
        id: halo
        anchors.centerIn: parent
        width: parent.width * 0.74; height: parent.height * 0.8
        opacity: 0.55
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: halo.width / 2; centerY: halo.height / 2
                centerRadius: halo.width / 2; focalX: centerX; focalY: centerY
                GradientStop { position: 0; color: "#3815c7e9" }
                GradientStop { position: 0.5; color: "#14057493" }
                GradientStop { position: 1; color: "#0003101e" }
            }
            PathAngleArc { centerX: halo.width / 2; centerY: halo.height / 2; radiusX: halo.width / 2; radiusY: halo.height / 2; startAngle: 0; sweepAngle: 360 }
        }
        SequentialAnimation on opacity {
            running: root.moving; loops: Animation.Infinite
            NumberAnimation { to: 1.0; duration: 3500 * root.pace; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.55; duration: 3500 * root.pace; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on scale {
            running: root.moving; loops: Animation.Infinite
            NumberAnimation { to: 1.035; duration: 3500 * root.pace; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.96; duration: 3500 * root.pace; easing.type: Easing.InOutSine }
        }
    }
    // Ribbons (pulse like 0.7's travelling dash).
    Image {
        anchors.fill: parent
        source: "assets/flow-ribbons.svg"
        sourceSize: Qt.size(width * 2, height * 2)
        fillMode: Image.PreserveAspectFit
        opacity: 0.7
        SequentialAnimation on opacity {
            running: root.moving; loops: Animation.Infinite
            NumberAnimation { to: 0.95; duration: 5500 * root.pace; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.6; duration: 5500 * root.pace; easing.type: Easing.InOutSine }
        }
    }
    // The play mark floats.
    Image {
        id: mark
        width: parent.height * 0.42; height: width * 190 / 180
        x: parent.width / 2 - width * 80 / 180; y: parent.height * 0.49 - height / 2
        source: "assets/flow-mark.svg"
        sourceSize: Qt.size(width * 2, height * 2)
        transform: Translate { id: lift; y: 2 }
        SequentialAnimation {
            running: root.moving; loops: Animation.Infinite
            NumberAnimation { target: lift; property: "y"; to: -5; duration: 3000 * root.pace; easing.type: Easing.InOutSine }
            NumberAnimation { target: lift; property: "y"; to: 2; duration: 3000 * root.pace; easing.type: Easing.InOutSine }
        }
    }
}
