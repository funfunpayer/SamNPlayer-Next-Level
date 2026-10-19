import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import SamN

// Settings behind the gear (D-146; 0.7 Tools → Settings / Log / About): a
// page over the current mode, Esc or × goes back to it.
Item {
    id: root
    property bool open: false
    // The Player (script folders, reload after "Honor inverted").
    property PlayerController player: null
    property int tab: 0
    readonly property alias settings: ctl
    visible: opacity > 0
    opacity: open ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }

    SettingsController { id: ctl }
    FolderDialog {
        id: folderDialog
        title: qsTr("Add a script folder")
        onAccepted: if (root.player) root.player.addScriptFolder(selectedFolder.toString())
    }

    // Dim the app behind; a click outside the card closes.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(1 / 255, 7 / 255, 15 / 255, 0.78)
        MouseArea { anchors.fill: parent; onClicked: root.open = false }
    }
    // Opaque under the glass: the mode behind must not show through the text.
    Rectangle {
        anchors.fill: card; scale: card.scale
        radius: Theme.radius; color: Theme.bgBottom
    }
    GlassCard {
        id: card
        objectName: "settingsCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 980); height: Math.min(parent.height - 48, 720)
        scale: root.open ? 1 : 0.97
        Behavior on scale { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }  // clicks on the card stay on it
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 20; spacing: 14
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Icon { name: "settings"; color: Theme.cyan; width: 22; height: 22 }
                Text {
                    textFormat: Text.PlainText
                    text: qsTr("Settings"); color: Theme.text; font.family: Theme.font; font.pixelSize: 20; font.weight: Font.DemiBold
                }
                Item { width: 12 }
                FlowTabs {
                    objectName: "settingsTabs"
                    model: [qsTr("General"), qsTr("Log"), qsTr("About")]
                    current: root.tab
                    onPicked: (i) => root.tab = i
                }
                Item { Layout.fillWidth: true }
                IconButton {
                    objectName: "settingsClose"
                    icon: "close"; tip: qsTr("Close (Esc)")
                    onClicked: root.open = false
                }
            }

            // ---- General ----
            Flickable {
                objectName: "generalTab"
                visible: root.tab === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                contentHeight: general.implicitHeight
                ColumnLayout {
                    id: general
                    width: parent.width; spacing: 14
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: playCol.implicitHeight + 28
                        ColumnLayout {
                            id: playCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Play"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("Honor inverted flag — a script marked \"inverted\" plays flipped (100 − position)")
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowToggle {
                                    objectName: "honorInverted"
                                    checked: ctl.honorInverted
                                    onToggled: (c) => {
                                        ctl.honorInverted = c
                                        // 0.7: changing it reloads the open script.
                                        if (root.player) root.player.reloadScript()
                                    }
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: qsTr("Script folders"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                            Repeater {
                                model: root.player ? root.player.scriptFolders : []
                                delegate: RowLayout {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Text {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true; elide: Text.ElideMiddle
                                        text: modelData; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                    FlowButton {
                                        objectName: "removeFolder_" + index
                                        text: qsTr("Remove")
                                        onClicked: root.player.removeScriptFolder(modelData)
                                    }
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                visible: !root.player || root.player.scriptFolders.length === 0
                                text: qsTr("No folders yet — only the video's own folder is searched.")
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowButton {
                                objectName: "addFolder"
                                text: qsTr("Add folder…")
                                enabled: root.player !== null
                                onClicked: folderDialog.open()
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: devCol.implicitHeight + 28
                        ColumnLayout {
                            id: devCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Device"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                                    text: qsTr("On connect, run a short connection test (a brief vibration and suction pulse)")
                                    color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowToggle {
                                    objectName: "connectTest"
                                    checked: ctl.connectTest
                                    onToggled: (c) => ctl.connectTest = c
                                }
                            }
                        }
                    }
                    GlassCard {
                        Layout.fillWidth: true; implicitHeight: logCol.implicitHeight + 28
                        ColumnLayout {
                            id: logCol
                            anchors.fill: parent; anchors.margins: 14; spacing: 10
                            Text { textFormat: Text.PlainText; text: qsTr("Logs and files"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                            RowLayout {
                                spacing: 10
                                Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("Log level"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                                }
                                FlowCombo {
                                    objectName: "logLevel"
                                    Layout.preferredWidth: 160
                                    model: ctl.logLevels
                                    currentIndex: Math.max(0, ctl.logLevels.indexOf(ctl.logLevel))
                                    onActivated: ctl.logLevel = currentText
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: qsTr("Log file: %1").arg(ctl.logFile !== "" ? ctl.logFile : qsTr("(no log file written yet)"))
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: qsTr("Settings and data: %1").arg(ctl.configFolder)
                                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            Row {
                                spacing: 8
                                FlowButton { text: qsTr("Open log folder"); enabled: ctl.logFile !== ""; onClicked: ctl.openLogFolder() }
                                FlowButton { text: qsTr("Open settings folder"); onClicked: ctl.openConfigFolder() }
                            }
                        }
                    }
                }
            }

            // ---- Log (0.7 log.js) ----
            ColumnLayout {
                objectName: "logTab"
                visible: root.tab === 1
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    FlowToggle {
                        objectName: "warningsOnly"
                        checked: ctl.warningsOnly
                        onToggled: (c) => ctl.warningsOnly = c
                    }
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Warnings/errors only"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        id: logStatus
                        objectName: "logStatus"
                        textFormat: Text.PlainText
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    FlowButton {
                        objectName: "copyLog"
                        text: qsTr("Copy all")
                        onClicked: logStatus.text = ctl.copyLog() ? qsTr("Copied.") : qsTr("Nothing to copy.")
                    }
                    FlowButton {
                        objectName: "clearLog"
                        text: qsTr("Clear")
                        onClicked: { ctl.clearLog(); logStatus.text = qsTr("Cleared.") }
                    }
                    FlowButton { text: qsTr("Open folder"); enabled: ctl.logFile !== ""; onClicked: ctl.openLogFolder() }
                }
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: Qt.rgba(0, 0, 0, 0.25); border.color: Theme.cardBorder
                    ScrollView {
                        id: logScroll
                        anchors.fill: parent; anchors.margins: 8
                        TextArea {
                            id: logView
                            objectName: "logView"
                            readOnly: true; selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.NoWrap
                            text: ctl.logText !== "" ? ctl.logText : qsTr("Nothing logged yet.")
                            color: Theme.text; font.family: "monospace"; font.pixelSize: 12
                            background: null
                            // Newest at the bottom, as 0.7.
                            onTextChanged: cursorPosition = length
                        }
                    }
                }
            }

            // ---- About ----
            ColumnLayout {
                objectName: "aboutTab"
                visible: root.tab === 2
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 12
                RowLayout {
                    spacing: 16
                    Image { source: "assets/logo.png"; Layout.preferredWidth: 72; Layout.preferredHeight: 72; sourceSize: Qt.size(144, 144) }
                    ColumnLayout {
                        spacing: 4
                        Text { textFormat: Text.PlainText; text: "SamNPlayer 2"; color: Theme.orange; font.family: Theme.font; font.pixelSize: 26; font.weight: Font.DemiBold }
                        Text {
                            objectName: "aboutVersion"
                            textFormat: Text.PlainText
                            text: qsTr("Version %1 · free and open source (GPL-3.0-or-later)").arg(ctl.version !== "" ? ctl.version : "dev")
                            color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: qsTr("Plays scripts with video and your device, creates them from a video, and guides training.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                    }
                }
                Row {
                    spacing: 8
                    FlowButton { objectName: "showLicence"; text: qsTr("Licence"); checked: legal.which === 0; onClicked: legal.which = 0 }
                    FlowButton { objectName: "showThirdParty"; text: qsTr("Third-party notices"); checked: legal.which === 1; onClicked: legal.which = 1 }
                }
                Rectangle {
                    id: legal
                    property int which: 0
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 10; color: Qt.rgba(0, 0, 0, 0.25); border.color: Theme.cardBorder
                    ScrollView {
                        anchors.fill: parent; anchors.margins: 8
                        TextArea {
                            objectName: "legalText"
                            readOnly: true; selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            text: legal.which === 0 ? ctl.licenceText : ctl.thirdPartyText
                            color: Theme.text; font.family: "monospace"; font.pixelSize: 12
                            background: null
                        }
                    }
                }
            }
        }
    }
}
