import QtQuick
import SamN

// The Library column of the Player (0.7 v1): local files, recent, favorites,
// the scripts imported / generated / edited, analysis shortcuts and a drop
// zone. Collapses to a thin strip (0.7 "⟨ / ⟩").
GlassCard {
    id: root
    required property LibraryController library
    property bool collapsed: false
    property string currentPath
    signal openFile()
    signal openPath(string path)
    signal showAudioAnalysis()
    signal showBpm()
    width: collapsed ? 44 : 248
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    component Title: Text {
        color: Theme.textMuted
        font.family: Theme.font; font.pixelSize: 11; font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
        topPadding: 10; bottomPadding: 4
    }
    component Item_: Rectangle {
        id: it
        property alias text: label.text
        property string glyph
        property string count
        property bool primary: false
        property bool expanded: false
        signal clicked()
        width: parent ? parent.width : 200; height: 34; radius: 10
        color: primary ? Qt.rgba(0, 120 / 255, 1, 0.22) : area.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
        border.color: primary ? Theme.cardBorder : "transparent"
        Text { x: 10; anchors.verticalCenter: parent.verticalCenter; text: it.glyph; color: Theme.cyan; font.pixelSize: 14 }
        Text {
            id: label
            x: 34; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 70; elide: Text.ElideRight
            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            text: it.count; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: it.clicked() }
    }
    component Files: Column {
        id: files
        property var model: []
        property string emptyText: qsTr("Nothing yet")
        width: parent ? parent.width : 200
        Text {
            visible: files.model.length === 0
            leftPadding: 34; text: files.emptyText
            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        Repeater {
            model: files.model
            delegate: Rectangle {
                required property var modelData
                width: files.width; height: 28; radius: 8
                color: fa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                Text {
                    x: 34; width: parent.width - 60; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name; elide: Text.ElideMiddle
                    color: !modelData.exists ? Theme.textMuted : modelData.path === root.currentPath ? Theme.cyan : Theme.textDim
                    font.family: Theme.fontSoft; font.pixelSize: 12; font.strikeout: !modelData.exists
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: root.library.isFavorite(modelData.path) ? "★" : "☆"
                    color: Theme.orange; font.pixelSize: 13
                    MouseArea {
                        anchors.fill: parent; anchors.margins: -4; cursorShape: Qt.PointingHandCursor
                        onClicked: root.library.toggleFavorite(modelData.path)
                    }
                }
                MouseArea {
                    id: fa
                    anchors.fill: parent; anchors.rightMargin: 26; hoverEnabled: true
                    cursorShape: modelData.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
                    ToolTipText { text: modelData.path; shown: fa.containsMouse }
                    onClicked: if (modelData.exists) root.openPath(modelData.path)
                }
            }
        }
    }

    // Collapse toggle (top right) — more room for the video.
    Rectangle {
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Text { anchors.centerIn: parent; text: root.collapsed ? "⟩" : "⟨"; color: Theme.textDim; font.pixelSize: 15 }
        MouseArea { id: tg; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.collapsed = !root.collapsed }
    }

    Flickable {
        visible: !root.collapsed
        anchors.fill: parent; anchors.margins: 12; anchors.topMargin: 6
        contentHeight: col.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width
            spacing: 2
            Title { text: qsTr("Library") }
            Item_ { text: qsTr("Local Files"); glyph: "📁"; primary: true; onClicked: root.openFile() }
            Item_ { id: recentHead; text: qsTr("Recent"); glyph: "🕘"; expanded: true; onClicked: expanded = !expanded }
            Files { visible: recentHead.expanded; model: root.library.recent; emptyText: qsTr("No recent scripts") }
            Item_ { id: favHead; text: qsTr("Favorites"); glyph: "★"; expanded: true; count: root.library.favorites.length || ""; onClicked: expanded = !expanded }
            Files { visible: favHead.expanded; model: root.library.favorites; emptyText: qsTr("Star a script to keep it here") }
            Title { text: qsTr("Scripts") }
            Item_ { id: impHead; text: qsTr("Imported"); glyph: "⤓"; count: root.library.imported.length || ""; onClicked: expanded = !expanded }
            Files { visible: impHead.expanded; model: root.library.imported }
            Item_ { id: genHead; text: qsTr("Generated"); glyph: "✦"; count: root.library.generated.length || ""; onClicked: expanded = !expanded }
            Files { visible: genHead.expanded; model: root.library.generated; emptyText: qsTr("The Generator's scripts land here") }
            Item_ { id: editHead; text: qsTr("Edited"); glyph: "✎"; count: root.library.edited.length || ""; onClicked: expanded = !expanded }
            Files { visible: editHead.expanded; model: root.library.edited }
            Title { text: qsTr("Analysis") }
            Item_ { text: qsTr("Audio Analysis"); glyph: "〰"; onClicked: root.showAudioAnalysis() }
            Item_ { text: qsTr("BPM Detection"); glyph: "▮"; onClicked: root.showBpm() }
            Title { text: qsTr("Quick Access") }
            Rectangle {
                width: parent.width; height: 74; radius: 12
                color: Qt.rgba(1, 1, 1, 0.02)
                border.color: Theme.cardBorder; border.width: 1
                Column {
                    anchors.centerIn: parent; spacing: 2
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Drag files here"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Video or script → Player"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11 }
                }
            }
        }
    }
}
