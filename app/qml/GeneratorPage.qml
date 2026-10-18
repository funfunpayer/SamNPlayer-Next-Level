import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import SamN

// The Generator mode (P3.3, 0.7 generator.js): 1 Video · 2 Tip · 3 Feel ·
// 4 Create · 5 Review. The Generator is the Editor (D-137): the result opens
// in the Player for review.
Item {
    id: page
    property bool deckOpen: false
    // The Player's video, for "Use Player video".
    property url playerVideoUrl
    property alias controller: gen
    signal reviewRequested(string path)

    GeneratorController {
        id: gen
        onCreated: (path) => page.reviewRequested(path)
    }

    FileDialog {
        id: videoDialog
        title: qsTr("Choose a video")
        nameFilters: [qsTr("Video (*.mp4 *.mkv *.webm *.mov *.avi *.m4v)"), qsTr("All files (*)")]
        onAccepted: gen.openVideo(selectedFile.toString())
    }
    MessageDialog {
        id: overwriteDialog
        title: qsTr("Replace the script?")
        text: qsTr("A script already exists for this video. Creating again replaces these files.")
        buttons: MessageDialog.Ok | MessageDialog.Cancel
        onAccepted: gen.create(true)
    }
    function startCreate() {
        if (gen.scriptExists) overwriteDialog.open()
        else gen.create(false)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 16

        // ---- left: the steps ----
        ColumnLayout {
            Layout.preferredWidth: 360; Layout.maximumWidth: 360
            Layout.fillHeight: true
            spacing: 12

            Text {
                textFormat: Text.PlainText
                text: qsTr("Create Emotion Script")
                color: Theme.text; font.family: Theme.font; font.pixelSize: 22; font.weight: Font.DemiBold
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("From a quiet video — motion becomes feel you can play.")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }

            // Step chips: done ✓, current highlighted.
            Row {
                objectName: "steps"
                spacing: 6
                Repeater {
                    model: [qsTr("Video"), qsTr("Tip"), qsTr("Feel"), qsTr("Create"), qsTr("Review")]
                    delegate: Rectangle {
                        required property int index
                        required property string modelData
                        readonly property bool current: gen.step === index + 1
                        readonly property bool done: gen.step > index + 1
                        height: 26; width: chip.implicitWidth + 18; radius: 13
                        color: current ? Qt.rgba(0, 217 / 255, 244 / 255, 0.22) : Qt.rgba(1, 1, 1, 0.04)
                        border.color: current ? Theme.cyan : Theme.cardBorder
                        Text {
                            id: chip
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            text: (parent.done ? "✓ " : (parent.index + 1) + " ") + parent.modelData
                            color: parent.current ? Theme.text : Theme.textDim
                            font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
            }

            // 1 Video
            GlassCard {
                Layout.fillWidth: true; implicitHeight: videoCol.implicitHeight + 28
                ColumnLayout {
                    id: videoCol
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("1 · Video"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    Text {
                        objectName: "videoLabel"
                        textFormat: Text.PlainText
                        Layout.fillWidth: true; elide: Text.ElideMiddle
                        text: gen.hasVideo ? gen.videoName : qsTr("No video selected")
                        color: gen.hasVideo ? Theme.text : Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    Text {
                        textFormat: Text.PlainText
                        visible: gen.hasVideo
                        text: gen.videoInfo; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Flow {
                        Layout.fillWidth: true; spacing: 8
                        FlowButton {
                            objectName: "chooseVideo"
                            text: qsTr("Choose video…"); primary: !gen.hasVideo
                            enabled: !gen.running
                            onClicked: videoDialog.open()
                        }
                        FlowButton {
                            objectName: "usePlayerVideo"
                            visible: page.playerVideoUrl.toString() !== ""
                            enabled: !gen.running
                            text: qsTr("Use Player video")
                            onClicked: gen.openVideo(page.playerVideoUrl.toString())
                        }
                    }
                }
            }

            // 3 Feel
            GlassCard {
                Layout.fillWidth: true; implicitHeight: feelCol.implicitHeight + 28
                opacity: gen.hasVideo ? 1 : 0.5
                ColumnLayout {
                    id: feelCol
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("3 · How it feels"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    FlowCombo {
                        objectName: "styleCombo"
                        Layout.fillWidth: true
                        enabled: !gen.running
                        textRole: "label"; valueRole: "value"
                        model: [{ label: qsTr("Normal"), value: "standard" },
                                { label: qsTr("Soft"), value: "weich" },
                                { label: qsTr("Autotune"), value: "autotune" }]
                        currentIndex: Math.max(0, indexOfValue(gen.profile))
                        onActivated: gen.profile = currentValue
                    }
                    Repeater {
                        model: [{ text: qsTr("Invert motion direction"), key: "invert" },
                                { text: qsTr("Camera motion compensation"), key: "cameraCompensation" },
                                { text: qsTr("Scene-cut detection"), key: "sceneCuts" },
                                { text: qsTr("Suggest O-zone automatically"), key: "autoOZone" }]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: modelData.text; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                            }
                            FlowToggle {
                                objectName: "toggle_" + modelData.key
                                enabled: !gen.running
                                checked: gen[modelData.key]
                                onToggled: (c) => gen[modelData.key] = c
                            }
                        }
                    }
                }
            }

            // 4 Create
            GlassCard {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 14; spacing: 8
                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("4 · Create"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                    }
                    RowLayout {
                        spacing: 8
                        FlowButton {
                            objectName: "createButton"
                            text: qsTr("Create Emotion Script"); primary: true
                            enabled: gen.hasVideo && gen.hasBox && !gen.running
                            onClicked: page.startCreate()
                        }
                        FlowButton {
                            objectName: "cancelButton"
                            text: qsTr("Cancel"); visible: gen.running
                            onClicked: gen.cancel()
                        }
                    }
                    Rectangle {
                        objectName: "progressBar"
                        Layout.fillWidth: true; height: 6; radius: 3
                        visible: gen.running || gen.percent > 0
                        color: Qt.rgba(1, 1, 1, 0.08)
                        Rectangle {
                            height: parent.height; radius: 3
                            // Unknown length: a full, faded bar (0.7).
                            width: gen.percent < 0 ? parent.width : parent.width * gen.percent / 100
                            opacity: gen.percent < 0 ? 0.35 : 1
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: Theme.blue }
                                GradientStop { position: 1; color: Theme.cyan }
                            }
                            Behavior on width { NumberAnimation { duration: Theme.animFast } }
                        }
                    }
                    Text {
                        objectName: "progressText"
                        textFormat: Text.PlainText
                        visible: gen.progressText !== ""
                        text: gen.progressText; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                    Text {
                        objectName: "status"
                        textFormat: Text.PlainText
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: gen.status; color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 13
                    }
                    FlowButton {
                        objectName: "reviewButton"
                        visible: gen.outputPath !== ""
                        text: qsTr("Review in the Player")
                        onClicked: page.reviewRequested(gen.outputPath)
                    }
                    ListView {
                        objectName: "runLog"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; spacing: 2
                        model: gen.log
                        delegate: Text {
                            required property string modelData
                            textFormat: Text.PlainText
                            width: ListView.view.width; wrapMode: Text.WordWrap
                            text: modelData; color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
            }
        }

        // ---- centre: 2 Tip — the marking frame ----
        GlassCard {
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                Text {
                    textFormat: Text.PlainText
                    text: qsTr("2 · Tip — drag a box around the moving tip")
                    color: Theme.text; font.family: Theme.font; font.pixelSize: 15
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    FrameItem {
                        id: frameView
                        objectName: "frameView"
                        anchors.fill: parent
                        image: gen.frame
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        visible: !gen.hasVideo
                        text: qsTr("Start here: choose a short clip, drag the tip box, create.")
                        color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 14
                    }
                    // Video pixels <-> item coordinates.
                    readonly property real scaleX: gen.videoSize.width > 0 ? frameView.imageRect.width / gen.videoSize.width : 1
                    readonly property real scaleY: gen.videoSize.height > 0 ? frameView.imageRect.height / gen.videoSize.height : 1
                    // The tip box: teal, as 0.7 (#3dccc0).
                    Rectangle {
                        objectName: "tipBox"
                        visible: gen.hasBox || drag.active
                        readonly property rect r: drag.active ? drag.rect : gen.box
                        x: frameView.imageRect.x + r.x * parent.scaleX
                        y: frameView.imageRect.y + r.y * parent.scaleY
                        width: r.width * parent.scaleX; height: r.height * parent.scaleY
                        color: Qt.rgba(61 / 255, 204 / 255, 192 / 255, 0.16)
                        border.color: "#3dccc0"; border.width: 2
                    }
                    MouseArea {
                        id: drag
                        objectName: "markArea"
                        anchors.fill: parent
                        enabled: gen.hasVideo && !gen.running
                        cursorShape: Qt.CrossCursor
                        property bool active: false
                        property point start
                        property rect rect
                        function toVideo(mx, my) {
                            return Qt.point((mx - frameView.imageRect.x) / parent.scaleX, (my - frameView.imageRect.y) / parent.scaleY)
                        }
                        onPressed: (m) => { start = toVideo(m.x, m.y); rect = Qt.rect(start.x, start.y, 0, 0); active = true }
                        function spanTo(mx, my) {
                            const p = toVideo(mx, my)
                            rect = Qt.rect(Math.min(start.x, p.x), Math.min(start.y, p.y), Math.abs(p.x - start.x), Math.abs(p.y - start.y))
                        }
                        onPositionChanged: (m) => spanTo(m.x, m.y)
                        // The box ends where the button is let go.
                        onReleased: (m) => {
                            spanTo(m.x, m.y)
                            active = false
                            gen.box = Qt.rect(Math.round(rect.x), Math.round(rect.y), Math.round(rect.width), Math.round(rect.height))
                        }
                    }
                }
                Text {
                    objectName: "boxLabel"
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; elide: Text.ElideRight
                    visible: gen.hasVideo
                    text: gen.boxLabel; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                }
                RowLayout {
                    visible: gen.hasVideo
                    spacing: 10
                    IconButton { objectName: "frameBack"; icon: "back10"; onClicked: gen.seekPreview(gen.previewMs - 10000) }
                    FlowSlider {
                        objectName: "frameSlider"
                        Layout.fillWidth: true
                        from: 0; to: Math.max(1, gen.durationMs); value: gen.previewMs
                        onMoved: (v) => gen.seekPreview(v)
                    }
                    IconButton { objectName: "frameForward"; icon: "forward10"; onClicked: gen.seekPreview(gen.previewMs + 10000) }
                    Text {
                        textFormat: Text.PlainText
                        text: Math.floor(gen.previewMs / 60000) + ":" + String(Math.floor(gen.previewMs / 1000) % 60).padStart(2, "0")
                              + " / " + Math.floor(gen.durationMs / 60000) + ":" + String(Math.floor(gen.durationMs / 1000) % 60).padStart(2, "0")
                        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                    }
                }
            }
        }
    }
}
