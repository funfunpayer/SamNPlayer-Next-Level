import QtQuick
import SamN

// Script offset: − typed field + (buttons ±50 ms). Typed values step 10 ms,
// clamped ±10 000 (0.7 / PlayerController::setOffsetMs). Saved per script.
Row {
    id: root
    objectName: "offsetField"
    property int value: 0
    signal changed(int value)
    spacing: 6

    function format(v) {
        return (v > 0 ? "+" : "") + v + " ms"
    }
    function commitText() {
        let raw = input.text.replace(/ms/gi, "").replace(/\+/g, "").replace(/\s/g, "")
        let v = parseInt(raw, 10)
        if (isNaN(v)) {
            input.text = root.format(root.value)
            return
        }
        v = Math.round(v / 10) * 10
        if (v > 10000) v = 10000
        if (v < -10000) v = -10000
        input.text = root.format(v)
        if (v !== root.value) root.changed(v)
    }

    IconButton { width: 32; height: 32; icon: "minus"; onClicked: root.changed(root.value - 50) }
    Rectangle {
        width: 92; height: 32; radius: 10
        color: Qt.rgba(1, 1, 1, 0.03)
        border.color: input.activeFocus ? Theme.cyan : Theme.cardBorder
        TextInput {
            id: input
            objectName: "offsetInput"
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            color: root.value === 0 && !activeFocus ? Theme.textDim : Theme.text
            selectionColor: Theme.blue
            font.family: Theme.fontSoft
            font.pixelSize: 13
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            text: root.format(root.value)
            onEditingFinished: root.commitText()
            Keys.onReturnPressed: { commitText(); focus = false }
            Keys.onEnterPressed: { commitText(); focus = false }
        }
    }
    IconButton { width: 32; height: 32; icon: "plus"; onClicked: root.changed(root.value + 50) }

    onValueChanged: {
        if (!input.activeFocus)
            input.text = root.format(root.value)
    }
}
