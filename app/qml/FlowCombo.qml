import QtQuick
import QtQuick.Controls.Basic  // Basic: our own look on every system (the native Windows style ignores it)
import SamN

// A dropdown in the Flow look (variants, chapters).
ComboBox {
    id: root
    implicitHeight: 34
    font.family: Theme.fontSoft
    font.pixelSize: 13
    background: Rectangle {
        radius: 10
        color: root.hovered ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.03)
        border.color: root.activeFocus ? Theme.cyan : Theme.cardBorder
    }
    contentItem: Text {
        textFormat: Text.PlainText
        leftPadding: 12; rightPadding: 28
        text: root.displayText
        color: Theme.text; font: root.font
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: Text {
        textFormat: Text.PlainText
        x: root.width - width - 10; anchors.verticalCenter: parent.verticalCenter
        text: "⌄"; color: Theme.textDim; font.pixelSize: 14
    }
    popup.background: Rectangle { color: "#0a1a2e"; border.color: Theme.cardBorder; radius: 10 }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: root.width
        contentItem: Text { textFormat: Text.PlainText; text: root.textRole ? modelData[root.textRole] : modelData; color: Theme.text; font: root.font }
        highlighted: root.highlightedIndex === index
        background: Rectangle { color: parent.highlighted ? Qt.rgba(0, 120 / 255, 1, 0.30) : "transparent" }
    }
}
