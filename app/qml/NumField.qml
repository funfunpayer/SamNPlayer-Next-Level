import QtQuick
import SamN

// A small labelled number field of the script editor: the value commits when
// editing ends (Enter or focus lost) and is kept inside from..to.
Column {
    id: root
    property string label
    property real value: 0
    property real from: 0
    property real to: 1
    property int decimals: 2
    signal edited(real value)
    spacing: 2
    Text { textFormat: Text.PlainText; text: root.label; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 10 }
    Rectangle {
        width: 74; height: 26; radius: 8
        color: Qt.rgba(1, 1, 1, 0.03)
        border.color: input.activeFocus ? Theme.cyan : Theme.cardBorder
        TextInput {
            id: input
            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text; selectionColor: Theme.blue
            font.family: Theme.fontSoft; font.pixelSize: 12
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            text: Number(root.value).toFixed(root.decimals)
            onEditingFinished: {
                const v = parseFloat(text.replace(",", "."))
                const clamped = isNaN(v) ? root.value : Math.max(root.from, Math.min(root.to, v))
                text = Number(clamped).toFixed(root.decimals)
                if (clamped !== root.value) root.edited(clamped)
            }
        }
    }
}
