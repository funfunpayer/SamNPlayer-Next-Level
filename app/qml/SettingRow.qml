import QtQuick
import QtQuick.Layouts
import SamN

// A labelled slider row of the settings card: name, slider, current value.
RowLayout {
    id: root
    property string label
    property real from: 0
    property real to: 1
    property real step: 0.05
    property real value: 0
    property string display: String(value)
    property string tip
    // A 0.7 help key (HelpTexts.js): a "?" at the end of the row.
    property string help
    signal moved(real value)
    spacing: 10
    Text {
        textFormat: Text.PlainText
        Layout.preferredWidth: 118
        text: root.label
        color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
        elide: Text.ElideRight
        MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true }
        ToolTipText { text: root.tip; shown: hover.containsMouse }
    }
    FlowSlider {
        Layout.fillWidth: true
        from: root.from; to: root.to; value: root.value
        onMoved: (v) => root.moved(Math.round(v / root.step) * root.step)
    }
    Rectangle {
        Layout.preferredWidth: 64; Layout.preferredHeight: 26
        radius: 8; color: Qt.rgba(1, 1, 1, 0.04); border.color: Theme.cardBorder
        Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: root.display; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12 }
    }
    HelpChip { key: root.help; visible: key !== "" && text !== "" }
}
