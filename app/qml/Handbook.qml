import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SamN
import "HandbookTexts.js" as HandbookCopy

// In-app user handbook (0.7 handbook.js / PARITY §12): FAQ for Create, Play,
// gaps, and the opt-in AI path. Opened from Settings and Generator.
Item {
    id: root
    objectName: "Handbook"
    property bool open: false
    property int activeSection: 0
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    onOpenChanged: if (open) {
        activeSection = 0
        bodyFlick.contentY = 0
    }

    readonly property var sections: HandbookCopy.sections
    // Lilac accent from the Flow lane palette (PARITY: "dark ink + lilac accent").
    readonly property color accent: Theme.laneSuction

    function openSection(i) {
        if (i < 0 || i >= sections.length) return
        activeSection = i
        const item = sectionRepeater.itemAt(i)
        if (!item) return
        const y = item.mapToItem(bodyCol, 0, 0).y
        bodyFlick.contentY = Math.max(0, Math.min(y - 8, bodyFlick.contentHeight - bodyFlick.height))
    }

    function syncActiveFromScroll() {
        if (!open || sections.length === 0) return
        let best = 0
        let bestDist = Number.POSITIVE_INFINITY
        const viewTop = bodyFlick.contentY + 24
        for (let i = 0; i < sectionRepeater.count; ++i) {
            const item = sectionRepeater.itemAt(i)
            if (!item) continue
            const y = item.mapToItem(bodyCol, 0, 0).y
            const dist = Math.abs(y - viewTop)
            if (y <= viewTop + 4 || dist < bestDist) {
                best = i
                bestDist = dist
            }
            if (y > viewTop + 4 && dist >= bestDist) break
        }
        activeSection = best
    }

    // Dim the app behind; a click outside the card closes.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1 / 255, 7 / 255, 15 / 255, 0.78)
        MouseArea { anchors.fill: parent; onClicked: root.open = false }
    }
    Rectangle {
        anchors.fill: card; scale: card.scale
        radius: Theme.radius; color: Theme.bgBottom
    }
    GlassCard {
        id: card
        objectName: "handbookCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 980); height: Math.min(parent.height - 48, 720)
        scale: root.open ? 1 : 0.96
        Behavior on scale { NumberAnimation { duration: Theme.animSlow; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }  // clicks on the card stay on it

        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 12

            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Icon { name: "info"; color: root.accent; width: 22; height: 22 }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Emotion Script"); color: Theme.textMuted
                        font.family: Theme.font; font.pixelSize: 11; font.letterSpacing: 1.4
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        objectName: "handbookTitle"
                        textFormat: Text.PlainText
                        text: qsTr("User handbook"); color: Theme.text
                        font.family: Theme.font; font.pixelSize: 20; font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        text: qsTr("How to use Create, Play, gaps, and the opt-in AI path. Default curve writer stays CSRT.")
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                }
                IconButton {
                    objectName: "handbookClose"
                    icon: "close"; tip: qsTr("Close (Esc)")
                    onClicked: root.open = false
                }
            }

            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 14

                // Left section nav.
                Flickable {
                    id: navFlick
                    objectName: "handbookNav"
                    Layout.preferredWidth: 220; Layout.fillHeight: true
                    clip: true; contentHeight: navCol.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: parent.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    }
                    Column {
                        id: navCol
                        width: parent.width; spacing: 2
                        Repeater {
                            model: root.sections
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                width: navCol.width; height: navLabel.implicitHeight + 14; radius: 10
                                color: root.activeSection === index
                                       ? Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.22)
                                       : navArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                                border.color: root.activeSection === index ? root.accent : "transparent"
                                Text {
                                    id: navLabel
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left; anchors.right: parent.right
                                    anchors.leftMargin: 10; anchors.rightMargin: 8
                                    textFormat: Text.PlainText; wrapMode: Text.WordWrap
                                    text: modelData.title
                                    color: root.activeSection === index ? Theme.text : Theme.textDim
                                    font.family: Theme.fontSoft; font.pixelSize: 12
                                    font.weight: root.activeSection === index ? Font.DemiBold : Font.Normal
                                }
                                MouseArea {
                                    id: navArea
                                    anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.openSection(index)
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1; Layout.fillHeight: true
                    color: Theme.cardBorder
                }

                // Body sections.
                Flickable {
                    id: bodyFlick
                    objectName: "handbookBody"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; contentHeight: bodyCol.height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: parent.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    }
                    onContentYChanged: root.syncActiveFromScroll()
                    Behavior on contentY {
                        enabled: !bodyFlick.moving && !bodyFlick.flicking
                        NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic }
                    }
                    Column {
                        id: bodyCol
                        width: bodyFlick.width; spacing: 22
                        Repeater {
                            id: sectionRepeater
                            model: root.sections
                            delegate: Column {
                                required property int index
                                required property var modelData
                                width: bodyCol.width; spacing: 8
                                Text {
                                    objectName: "handbookSectionTitle"
                                    width: parent.width; wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText; text: modelData.title
                                    color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold
                                }
                                Rectangle {
                                    width: 36; height: 2; radius: 1
                                    color: root.accent; opacity: 0.85
                                }
                                Text {
                                    objectName: "handbookSectionBody"
                                    width: parent.width; wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText; text: modelData.body
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13; lineHeight: 1.35
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text {
                    objectName: "handbookFooter"
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    text: qsTr("Full markdown for agents: docs/USER_HANDBOOK.md · Look: dark ink + lilac accent")
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11
                }
                FlowButton {
                    objectName: "handbookCloseButton"
                    text: qsTr("Close"); onClicked: root.open = false
                }
            }
        }
    }
}
