import QtQuick
import SamN

Rectangle {
    id: card
    default property alias content: body.data
    property string title
    // A 0.7 help key (HelpTexts.js): a "?" after the title.
    property string help
    width: parent ? parent.width : 280
    height: body.implicitHeight + head.height + 22
    radius: Theme.radiusSmall
    color: Qt.rgba(1, 1, 1, 0.025); border.color: Theme.cardBorder
    // Soft top catch-light (matches GlassCard).
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 1; height: 1; radius: 1
        color: Qt.rgba(180 / 255, 220 / 255, 255 / 255, 0.08)
    }
    Text {
        textFormat: Text.PlainText
        id: head
        x: 12; y: 10; text: card.title
        color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
    }
    HelpChip { key: card.help; visible: key !== "" && text !== ""; x: head.x + head.implicitWidth + 6; y: head.y + (head.height - height) / 2 }
    Column { id: body; x: 12; y: head.y + head.height + 8; width: parent.width - 24; spacing: 8 }
}
