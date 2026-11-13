import QtQuick
import QtQuick.Window
import SamN

// App mark: always request pixels at devicePixelRatio (capped at the PNG).
Image {
    id: root
    source: "assets/logo.png"
    width: 58
    height: 58
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true
    // logo.png is 256² — never ask for more than that.
    readonly property int px: Math.min(256, Math.max(1, Math.ceil(Math.max(width, height) * Screen.devicePixelRatio)))
    sourceSize: Qt.size(px, px)
}
