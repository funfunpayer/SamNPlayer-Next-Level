import QtQuick
import SamN
import QtQuick.Shapes

// The round glowing Play button (the app symbol): a cyan→orange ring that
// turns while playing.
Item {
    id: root
    property bool playing: false
    signal clicked()
    width: 72; height: 72

    Shape {
        id: ring
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4
        ShapePath {
            strokeWidth: 4
            strokeColor: "transparent"
            fillGradient: ConicalGradient {
                centerX: ring.width / 2; centerY: ring.height / 2; angle: 0
                GradientStop { position: 0.0; color: Theme.cyan }
                GradientStop { position: 0.45; color: Theme.blue }
                GradientStop { position: 0.75; color: Theme.orange }
                GradientStop { position: 1.0; color: Theme.cyan }
            }
            PathAngleArc {
                centerX: ring.width / 2; centerY: ring.height / 2
                radiusX: ring.width / 2; radiusY: ring.height / 2
                startAngle: 0; sweepAngle: 360
            }
        }
        RotationAnimator on rotation {
            from: 0; to: 360; duration: 2400
            loops: Animation.Infinite
            running: root.playing
        }
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 8; height: width; radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: "#0d3358" }
            GradientStop { position: 0.6; color: "#061a31" }
            GradientStop { position: 1; color: "#030c1a" }
        }
        scale: area.pressed ? 0.94 : (area.containsMouse ? 1.04 : 1.0)
        Behavior on scale { NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic } }
        // Play triangle / pause bars.
        Canvas {
            id: glyph
            anchors.centerIn: parent
            width: parent.width * 0.42; height: width
            property bool playing: root.playing
            onPlayingChanged: requestPaint()
            onPaint: {
                const c = getContext("2d");
                c.reset();
                c.fillStyle = Theme.cyanSoft;
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
