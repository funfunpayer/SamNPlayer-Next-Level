import QtQuick
import QtQuick.Shapes
import SamN

// One glowing gauge of the Training stage (0.8 #550 theme_flow.css rings stage,
// docs/visual-target/training.png): a lit shell ring that glows even at rest,
// a halo that grows with the level, the level as a bright arc from the top,
// and the name, a large percentage and a small wave mark in the dark centre.
Item {
    id: root
    property string label
    property real level: 0        // 0-1
    property color tint: Theme.trainVibration
    property bool live: false      // a session runs: the shell dims, the arc leads
    property bool motion: Qt.application.state === Qt.ApplicationActive
    implicitWidth: 236; implicitHeight: 236
    readonly property real radius_: Math.min(width, height) / 2 - 24
    // Soft follow so the arc feels fluid at ~20 Hz updates.
    property real shown: level
    Behavior on shown { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
    readonly property real sweep: Math.max(0, Math.min(1, shown)) * 360
    readonly property real cx: width / 2
    readonly property real cy: height / 2
    function tinted(a) { return Qt.rgba(root.tint.r, root.tint.g, root.tint.b, a) }

    // Idle / live halo breathe (0.8 flow-tr-halo-breathe ~3.2 s) — opacity only,
    // so the dark centre stays locked to the rings.
    property real haloBreath: 1.0
    SequentialAnimation on haloBreath {
        running: root.motion
        loops: Animation.Infinite
        NumberAnimation {
            to: root.live ? 1.35 : 1.12
            duration: root.live ? 1600 : 2800
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            to: 0.85
            duration: root.live ? 1600 : 2800
            easing.type: Easing.InOutSine
        }
    }

    Shape {
        id: rings
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        // Halo: wide and faint, stronger with the level; breath multiplies alpha.
        ShapePath {
            strokeColor: root.tinted((0.10 + root.shown * 0.42) * (root.live ? 0.42 : 0.32) * root.haloBreath); strokeWidth: 36; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // Shell: lit at rest, quieter while live so the arc leads.
        ShapePath {
            strokeColor: root.tinted((root.live ? 0.22 : 0.58) * 0.35); strokeWidth: 20; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        ShapePath {
            strokeColor: root.tinted(root.live ? 0.22 : 0.58); strokeWidth: 10; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // Track under the arc.
        ShapePath {
            strokeColor: Qt.rgba(160 / 255, 210 / 255, 240 / 255, 0.10); strokeWidth: 10; fillColor: "transparent"
            PathAngleArc { centerX: root.cx; centerY: root.cy; radiusX: root.radius_; radiusY: root.radius_; startAngle: 0; sweepAngle: 360 }
        }
        // The level arc with its glow and a bright core.
        ShapePath {
            strokeColor: root.tinted(0.20); strokeWidth: 28; fillColor: "transparent"; capStyle: ShapePath.RoundCap
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
        border.color: root.tinted(0.34); border.width: 6
        opacity: 0.65
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
            // Soft cyan/orange wash behind the % while live.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 18; height: parent.height + 8; radius: 10
                color: root.tinted(root.live ? 0.14 : 0.0)
                Behavior on color { ColorAnimation { duration: Theme.animNormal } }
            }
            Text {
                id: pct
                textFormat: Text.PlainText
                text: Math.round(root.shown * 100) + "%"
                color: "#f4fbff"; font.family: Theme.font; font.pixelSize: Math.max(28, root.radius_ * 0.52); font.weight: Font.Bold
            }
        }
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            name: "waveform"; color: root.tint; width: 34; height: 20
            opacity: root.live ? 1 : 0.75
            SequentialAnimation on opacity {
                running: root.live && root.motion
                loops: Animation.Infinite
                NumberAnimation { to: 0.55; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
            }
        }
    }
}
