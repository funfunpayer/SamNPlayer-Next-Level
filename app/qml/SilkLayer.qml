import QtQuick
import SamN

// Silk waves (0.7/0.8 #550 theme_flow.css): the blue band top-right and the
// orange band bottom-left drift slowly behind a stage. Put it first in a
// clipping parent; it never takes input. `running` pauses the drift (for
// example while the window is hidden or the user wants less motion).
Item {
    id: root
    property bool running: true
    property real strength: 1.0  // scales the opacities
    anchors.fill: parent
    enabled: false  // never takes input; declared first, so it stays behind its siblings

    // Each band is larger than the stage (inset -30% in the CSS) so the
    // drift never shows an edge.
    Image {
        id: top
        source: "assets/bg-top.svg"
        sourceSize.width: 1400
        fillMode: Image.PreserveAspectFit
        width: root.width * 1.04; height: root.height * 0.9
        x: root.width * 0.18; y: -root.height * 0.12
        asynchronous: true
        opacity: 0.55 * root.strength
        transformOrigin: Item.Center
        property real dx: 0
        property real dy: 0
        transform: Translate { x: top.dx; y: top.dy }
        // silk-drift-1: 32 s alternate, -4% / +2.8%, scale 1.07, opacity .52-.62.
        SequentialAnimation on dx {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { to: -root.width * 0.04; duration: 32000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 32000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on dy {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { to: root.height * 0.028; duration: 32000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 32000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on scale {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.07; duration: 32000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 32000; easing.type: Easing.InOutSine }
        }
    }
    Image {
        id: bottom
        source: "assets/bg-bottom.svg"
        sourceSize.width: 1200
        fillMode: Image.PreserveAspectFit
        width: root.width * 0.94; height: root.height * 0.85
        x: -root.width * 0.12; y: root.height * 0.3
        asynchronous: true
        opacity: 0.45 * root.strength
        transformOrigin: Item.Center
        property real dx: 0
        property real dy: 0
        transform: Translate { x: bottom.dx; y: bottom.dy }
        // silk-drift-2: 40 s alternate-reverse, +4.5% / -3.2%, scale 1.06.
        SequentialAnimation on dx {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { to: root.width * 0.045; duration: 40000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 40000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on dy {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { to: -root.height * 0.032; duration: 40000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 40000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on scale {
            running: root.running; loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.06; duration: 40000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 40000; easing.type: Easing.InOutSine }
        }
    }
}
