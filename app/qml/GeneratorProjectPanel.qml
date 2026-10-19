import QtQuick
import SamN

// The Generator's left column (D-154, 0.7 create_layout "Project nav", the
// target image create.png): the source clips (the current video and the
// queue), recent videos, the generated and edited scripts, and a drop hint.
// Collapses to a thin strip like the Player's Library.
GlassCard {
    id: root
    required property LibraryController library
    required property GeneratorController generator
    property bool collapsed: false
    property string currentScript
    signal chooseVideo()
    signal addVideos()
    signal openVideo(string path)
    signal openScript(string path)
    width: collapsed ? 44 : 248
    Behavior on width { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
    clip: true

    component Title: Text {
        textFormat: Text.PlainText
        color: Theme.textMuted
        font.family: Theme.font; font.pixelSize: 11; font.letterSpacing: 1.6; font.capitalization: Font.AllUppercase
        topPadding: 10; bottomPadding: 4
    }
    component Head: Rectangle {
        id: head
        property alias text: label.text
        property string icon
        property string count
        property bool primary: false
        signal clicked()
        width: parent ? parent.width : 200; height: 34; radius: 10
        color: primary ? Qt.rgba(0, 120 / 255, 1, 0.22) : area.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
        border.color: primary ? Theme.cardBorder : "transparent"
        Icon { x: 9; anchors.verticalCenter: parent.verticalCenter; name: head.icon; width: 17; height: 17; color: Theme.cyan }
        Text {
            id: label
            textFormat: Text.PlainText
            x: 34; anchors.verticalCenter: parent.verticalCenter; width: parent.width - 70; elide: Text.ElideRight
            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
        }
        Text {
            textFormat: Text.PlainText
            anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
            text: head.count; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: head.clicked() }
    }
    // A short list of files (at most six, as 0.7); a click opens one.
    component Files: Column {
        id: files
        property var model: []
        property string emptyText: qsTr("Nothing yet")
        property string current
        signal picked(string path)
        width: parent ? parent.width : 200
        Text {
            textFormat: Text.PlainText
            visible: files.model.length === 0
            leftPadding: 34; text: files.emptyText
            color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
        }
        Repeater {
            model: files.model.slice(0, 6)
            delegate: Rectangle {
                required property var modelData
                width: files.width; height: 28; radius: 8
                color: fa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                Text {
                    textFormat: Text.PlainText
                    x: 34; width: parent.width - 42; anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name; elide: Text.ElideMiddle
                    color: !modelData.exists ? Theme.textMuted : modelData.path === files.current ? Theme.cyan : Theme.textDim
                    font.family: Theme.fontSoft; font.pixelSize: 12; font.strikeout: !modelData.exists
                }
                MouseArea {
                    id: fa
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: modelData.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
                    ToolTipText { text: modelData.path; shown: fa.containsMouse }
                    onClicked: if (modelData.exists) files.picked(modelData.path)
                }
            }
        }
    }

    // Collapse toggle (top right).
    Rectangle {
        objectName: "projectPanelToggle"
        anchors.right: parent.right; anchors.rightMargin: 8; y: 10; z: 2
        width: 28; height: 28; radius: 14
        color: tg.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        Icon { anchors.centerIn: parent; name: root.collapsed ? "chevronRight" : "chevronLeft"; width: 16; height: 16; color: Theme.textDim }
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
            Title { text: qsTr("Project") }
            Head {
                objectName: "projectChooseVideo"
                text: root.generator.hasVideo ? root.generator.videoName : qsTr("Choose video…")
                icon: "video"; primary: true
                onClicked: root.chooseVideo()
            }
            Head {
                id: clipsHead
                text: qsTr("Source clips"); icon: "layers"
                count: root.generator.queue.length || ""
                onClicked: root.addVideos()
            }
            // The queue: each clip with its state (0.7 "Source clips (N)").
            Repeater {
                model: root.generator.queue
                delegate: Rectangle {
                    required property var modelData
                    width: col.width; height: 28; radius: 8
                    color: qa.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                    Text {
                        textFormat: Text.PlainText
                        x: 34; width: parent.width - 100; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name; elide: Text.ElideMiddle
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.status
                        color: modelData.status === "done" ? Theme.cyan : modelData.status === "failed" ? Theme.danger : Theme.textMuted
                        font.family: Theme.fontSoft; font.pixelSize: 11
                    }
                    MouseArea {
                        id: qa
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        ToolTipText { text: modelData.message || modelData.path; shown: qa.containsMouse }
                        onClicked: root.openVideo(modelData.path)
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                visible: root.generator.queue.length === 0
                leftPadding: 34; text: qsTr("Add several videos to create them in a row")
                width: col.width; wrapMode: Text.WordWrap
                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            Title { text: qsTr("Recent videos") }
            Files {
                objectName: "projectRecentVideos"
                model: root.library.recentVideos
                current: root.generator.videoPath
                emptyText: qsTr("Videos you open show here")
                onPicked: (p) => root.openVideo(p)
            }
            Title { text: qsTr("Scripts") }
            Head { id: genHead; text: qsTr("Generated"); icon: "effects"; count: root.library.generated.length || "" }
            Files {
                objectName: "projectGenerated"
                model: root.library.generated
                current: root.currentScript
                emptyText: qsTr("Your created scripts land here")
                onPicked: (p) => root.openScript(p)
            }
            Head { id: editHead; text: qsTr("Edited"); icon: "edit"; count: root.library.edited.length || "" }
            Files {
                objectName: "projectEdited"
                model: root.library.edited
                current: root.currentScript
                onPicked: (p) => root.openScript(p)
            }
            Item { width: 1; height: 10 }
            Rectangle {
                width: parent.width; height: 74; radius: 12
                color: Qt.rgba(1, 1, 1, 0.02)
                border.color: Theme.cardBorder; border.width: 1
                Column {
                    anchors.centerIn: parent; spacing: 2
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "download"; width: 20; height: 20; color: Theme.cyan }
                    Text { textFormat: Text.PlainText; anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Drag videos here"); color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13 }
                    Text { textFormat: Text.PlainText; anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("or use Choose video…"); color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 11 }
                }
            }
        }
    }
}
