import QtQuick
import QtQuick.Shapes
import QtQuick.Window
import SamN

// The Flow idle graphic for "Feel without film" (0.7 flow_fx / D-138): a
// breathing halo, cyan + amber ribbon trails and a floating play mark. Calm
// while idle, faster while playing, still when paused or under reduced
// motion. Decorative only — never on the empty home (that is Play + Open).
Item {
    id: root
    objectName: "flowStage"
    // "idle" | "playing" | "paused"
    property string stageState: "idle"
    property bool active: Qt.application.state === Qt.ApplicationActive
    readonly property bool moving: stageState !== "paused" && visible && active && !Theme.reducedMotion
    // Idle: halo 7 s, mark 6 s, trails 11 s / 13 s. Playing: 4 / 5.5 / 6.5.
    readonly property int haloMs: stageState === "playing" ? 4000 : 7000
    readonly property int markMs: stageState === "playing" ? 5500 : 6000
    readonly property int trailAMs: stageState === "playing" ? 6500 : 11000
    readonly property int trailBMs: stageState === "playing" ? 5500 : 13000
    implicitWidth: 420
    implicitHeight: 236

    Shape {
        id: halo
        anchors.centerIn: parent
        width: parent.width * 0.74
        height: parent.height * 0.8
        opacity: root.moving ? 0.55 : 0.72
        scale: 1.0
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: halo.width / 2
                centerY: halo.height / 2
                centerRadius: halo.width / 2
                focalX: centerX
                focalY: centerY
                GradientStop { position: 0; color: "#3815c7e9" }
                GradientStop { position: 0.5; color: "#14057493" }
                GradientStop { position: 1; color: "#0003101e" }
            }
            PathAngleArc {
                centerX: halo.width / 2
                centerY: halo.height / 2
                radiusX: halo.width / 2
                radiusY: halo.height / 2
                startAngle: 0
                sweepAngle: 360
            }
        }
        SequentialAnimation on opacity {
            running: root.moving
            loops: Animation.Infinite
            NumberAnimation { to: 1.0; duration: root.haloMs / 2; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.55; duration: root.haloMs / 2; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on scale {
            running: root.moving
            loops: Animation.Infinite
            NumberAnimation { to: 1.04; duration: root.haloMs / 2; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.97; duration: root.haloMs / 2; easing.type: Easing.InOutSine }
        }
    }

    // Ribbons: opacity + slight travel; two periods so cyan/amber never lock.
    Item {
        id: ribbons
        anchors.fill: parent
        Image {
            id: trailA
            anchors.fill: parent
            source: "assets/flow-ribbons.svg"
            sourceSize: Qt.size(Math.max(1, Math.ceil(width * Screen.devicePixelRatio)),
                                Math.max(1, Math.ceil(height * Screen.devicePixelRatio)))
            smooth: true
            fillMode: Image.PreserveAspectFit
            opacity: 0.62
            property real dx: 0
            transform: Translate { x: trailA.dx }
            SequentialAnimation on opacity {
                running: root.moving
                loops: Animation.Infinite
                NumberAnimation { to: 0.95; duration: root.trailAMs / 2; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.55; duration: root.trailAMs / 2; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on dx {
                running: root.moving
                loops: Animation.Infinite
                NumberAnimation { to: root.width * 0.016; duration: root.trailAMs / 2; easing.type: Easing.InOutSine }
                NumberAnimation { to: -root.width * 0.01; duration: root.trailAMs / 2; easing.type: Easing.InOutSine }
            }
        }
        // Soft second layer, alternate period (stands in for the amber trail travel).
        Image {
            id: trailB
            anchors.fill: parent
            source: "assets/flow-ribbons.svg"
            sourceSize: Qt.size(Math.max(1, Math.ceil(width * Screen.devicePixelRatio)),
                                Math.max(1, Math.ceil(height * Screen.devicePixelRatio)))
            smooth: true
            fillMode: Image.PreserveAspectFit
            opacity: 0.28
            property real dx: 0
            transform: Translate { x: trailB.dx }
            SequentialAnimation on opacity {
                running: root.moving
                loops: Animation.Infinite
                NumberAnimation { to: 0.55; duration: root.trailBMs / 2; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.22; duration: root.trailBMs / 2; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on dx {
                running: root.moving
                loops: Animation.Infinite
                NumberAnimation { to: -root.width * 0.014; duration: root.trailBMs / 2; easing.type: Easing.InOutSine }
                NumberAnimation { to: root.width * 0.012; duration: root.trailBMs / 2; easing.type: Easing.InOutSine }
            }
        }
    }

    Image {
        id: mark
        objectName: "flowStageMark"
        width: parent.height * 0.42
        height: width * 190 / 180
        x: parent.width / 2 - width * 80 / 180
        y: parent.height * 0.49 - height / 2
        source: "assets/flow-mark.svg"
        sourceSize: Qt.size(Math.max(1, Math.ceil(width * Screen.devicePixelRatio)),
                            Math.max(1, Math.ceil(height * Screen.devicePixelRatio)))
        smooth: true
        transform: Translate { id: lift; y: 0 }
        SequentialAnimation {
            running: root.moving
            loops: Animation.Infinite
            NumberAnimation { target: lift; property: "y"; to: -6; duration: root.markMs / 2; easing.type: Easing.InOutSine }
            NumberAnimation { target: lift; property: "y"; to: 3; duration: root.markMs / 2; easing.type: Easing.InOutSine }
        }
    }
}
