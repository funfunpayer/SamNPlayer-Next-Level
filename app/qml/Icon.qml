import QtQuick
import QtQuick.Window
import SamN

// A Flow icon (0.7 icon set) in any colour, sharp on every screen.
Image {
    id: root
    property string name
    property color color: Theme.text
    width: 20; height: 20
    // "#rrggbb" built by hand: Qt 6.4's compiled form of `"?" + color`
    // crashed while a page was created next to the Generator's switches
    // (shell_qml_test, 27 Oct). The provider reads only red, green and blue.
    source: name === "" ? "" : "image://icon/" + name + "?#" + hex(color.r) + hex(color.g) + hex(color.b)
    function hex(v) { return ("0" + Math.round(v * 255).toString(16)).slice(-2) }
    sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
    fillMode: Image.PreserveAspectFit
    smooth: true
    cache: true
}
