import QtQuick
import SamN

Row {
    id: sr
    property string label
    property string tip
    property string help
    property real from: 0
    property real to: 1
    property real value: 0
    property string valueText
    property bool active: true
    signal picked(real v)
    spacing: 8; height: 26
    Text {
        textFormat: Text.PlainText
        anchors.verticalCenter: parent.verticalCenter; width: 78
        text: sr.label; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
        HoverHandler { id: srHover }
        ToolTipText { shown: srHover.hovered && sr.tip !== ""; text: sr.tip }
    }
    FlowSlider {
        anchors.verticalCenter: parent.verticalCenter; width: 112
        enabled: sr.active; opacity: enabled ? 1 : 0.45
        from: sr.from; to: sr.to; value: sr.value
        // The knob follows the model again (a preset may change it later).
        onMoved: (v) => { sr.picked(v); value = Qt.binding(() => sr.value) }
    }
    Text { textFormat: Text.PlainText; anchors.verticalCenter: parent.verticalCenter; text: sr.valueText; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
    HelpChip { key: sr.help; visible: key !== "" && text !== ""; anchors.verticalCenter: parent.verticalCenter }
}
