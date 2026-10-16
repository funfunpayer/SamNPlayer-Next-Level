import QtQuick
import QtQuick.Window
import SamN

// A Flow icon (0.7 icon set) in any colour, sharp on every screen.
Image {
    id: root
    property string name
    property color color: Theme.text
    width: 20; height: 20
    source: name === "" ? "" : "image://icon/" + name + "?" + color
    sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
    fillMode: Image.PreserveAspectFit
    smooth: true
    cache: true
}
