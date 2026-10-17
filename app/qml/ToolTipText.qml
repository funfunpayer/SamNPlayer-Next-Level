import QtQuick
import QtQuick.Controls.Basic  // Basic: our own look on every system (the native Windows style ignores it)
import SamN

// A small tooltip in the Flow look, shown while `shown` is true.
ToolTip {
    id: tip
    property bool shown: false
    visible: shown && text !== ""
    delay: 500
    contentItem: Text { textFormat: Text.PlainText; text: tip.text; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
    background: Rectangle { color: "#0a1a2e"; border.color: Theme.cardBorder; radius: 8 }
}
