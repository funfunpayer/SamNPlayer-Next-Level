import QtQuick
import QtQuick.Controls.Basic
import SamN
import "HelpTexts.js" as Help

// 0.7's "?" help chip (help.js): a small round button beside a control; a
// click opens a popover with the control's 0.7 help text (HelpTexts.js,
// verbatim, by the 0.7 element id). It opens below the chip, flips above or
// moves left to stay inside the window; a second click, a click elsewhere
// or Esc closes it. One popover at a time.
Item {
    id: chip
    // The 0.7 element id, e.g. "pb-offset-bake"; or the text itself.
    property string key
    property string text: Help.text(key)
    readonly property alias open: pop.visible
    implicitWidth: 18; implicitHeight: 18
    visible: text !== ""
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Help")
    Accessible.description: text

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: area.containsMouse || pop.visible ? Qt.rgba(0, 217 / 255, 244 / 255, 0.18) : "transparent"
        border.color: area.containsMouse || pop.visible ? Theme.cyan : Theme.textDim
        border.width: 1
        Text {
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: "?"
            color: area.containsMouse || pop.visible ? Theme.cyan : Theme.textDim
            font.family: Theme.font; font.pixelSize: 11; font.bold: true
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent; anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // The press that closed the popover (outside it) is this click: no reopen.
        onClicked: if (pop.visible) pop.close(); else if (Date.now() - pop.closedAt > 250) pop.open()
    }

    Popup {
        id: pop
        objectName: "helpPopover"
        parent: chip.Window.contentItem
        // Below the chip, inside the window with 8 px to spare; above it when
        // there is no room below (0.7 help.js). Placed when it opens.
        readonly property real pad: 8
        width: Math.min(320, (parent ? parent.width : 400) - 2 * pad)
        onAboutToShow: {
            const at = chip.mapToItem(null, 0, 0)
            x = Math.max(pad, Math.min(at.x, parent.width - width - pad))
            const below = at.y + chip.height + 6
            const h = implicitHeight
            y = below + h > parent.height - pad ? Math.max(pad, at.y - h - 6) : below
        }
        padding: 10
        focus: true  // Esc reaches it
        property double closedAt: 0
        onClosed: closedAt = Date.now()
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animFast } }
        contentItem: Text {
            objectName: "helpPopoverText"
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            text: chip.text
            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12; lineHeight: 1.2
        }
        background: Rectangle { color: "#0a1a2e"; border.color: Theme.cardBorder; radius: 10 }
    }
}
