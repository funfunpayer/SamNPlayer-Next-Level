import QtQuick
import QtQuick.Layouts
import SamN

// AI Train and Benchmark (D-155, 0.7 roi_training.js / benchmark.js): a plugin
// mode, shown when Settings → General → AI Train (preview) is on. This is the
// frame: the two pages and the 0.7 texts; the steps follow one by one
// (docs/P4_AI_TRAIN.md §3).
Item {
    id: page
    property bool overlayOpen: false
    property int tab: 0

    component Step: GlassCard {
        id: step
        property string number
        property string title
        property string text
        property string status
        Layout.fillWidth: true
        implicitHeight: stepCol.implicitHeight + 28
        ColumnLayout {
            id: stepCol
            anchors.fill: parent; anchors.margins: 14; spacing: 6
            RowLayout {
                spacing: 10
                Rectangle {
                    width: 26; height: 26; radius: 13
                    color: Qt.rgba(0, 120 / 255, 1, 0.25); border.color: Theme.cyan
                    Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: step.number; color: Theme.text; font.family: Theme.font; font.pixelSize: 13 }
                }
                Text { textFormat: Text.PlainText; text: step.title; color: Theme.text; font.family: Theme.font; font.pixelSize: 16 }
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: step.text
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: step.status
                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12; font.italic: true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            FlowTabs {
                objectName: "aiTrainTabs"
                model: [qsTr("AI Train"), qsTr("Benchmark")]
                current: page.tab
                onPicked: (i) => page.tab = i
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; elide: Text.ElideRight
                text: qsTr("Preview — turn it off in Settings → General.")
                color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
            }
        }

        Flickable {
            objectName: "aiTrainPageBody"
            Layout.fillWidth: true; Layout.fillHeight: true
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: body
                width: parent.width
                spacing: 12

                // ---- AI Train ----
                GlassCard {
                    visible: page.tab === 0
                    Layout.fillWidth: true
                    implicitHeight: introCol.implicitHeight + 28
                    ColumnLayout {
                        id: introCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("AI training"); color: Theme.text; font.family: Theme.font; font.pixelSize: 18 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: qsTr("Teachers propose the boxes, you confirm or correct them, and our own small model learns from what you confirmed. It then finds the tip in the Generator: it only suggests, classic tracking still writes the script. Everything stays on this computer or on a server in your own network.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                    }
                }
                Step {
                    visible: page.tab === 0
                    number: "1"; title: qsTr("Label")
                    text: qsTr("Open a clip or an image. Each label (glans, mouth, hand …) is a track through the clip with its own lane under the video. Scrub or play: the boxes ride along. Where one drifts, drag it back; the tracker fills the stretch to the next correction. Mark where a label is not visible, and jump to the next unsure frame.")
                    status: qsTr("Comes next (D-155 steps 2 and 3).")
                }
                Step {
                    visible: page.tab === 0
                    number: "2"; title: qsTr("Teachers")
                    text: qsTr("NudeNet (one click to install) and vision models on Ollama, LM Studio or a server in your network, such as a Mac with larger models, propose boxes. Where several agree, the proposal is offered; you confirm it in the label editor.")
                    status: qsTr("D-155 step 4.")
                }
                Step {
                    visible: page.tab === 0
                    number: "3"; title: qsTr("Train our model")
                    text: qsTr("Every N-th frame of the confirmed stretches becomes a sample, split by clip. RF-DETR learns from them and is exported as one ONNX file; training runs in a separate Python set up with one click (CUDA, DirectML, Apple MPS or CPU).")
                    status: qsTr("D-155 step 5.")
                }
                Step {
                    visible: page.tab === 0
                    number: "4"; title: qsTr("Use it")
                    text: qsTr("When our model beats the teachers on the benchmark, it becomes the fast teacher and the Generator's Find tip uses it.")
                    status: qsTr("D-155 steps 5 and 6.")
                }

                // ---- Benchmark ----
                GlassCard {
                    visible: page.tab === 1
                    Layout.fillWidth: true
                    implicitHeight: benchCol.implicitHeight + 28
                    ColumnLayout {
                        id: benchCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Benchmark"); color: Theme.text; font.family: Theme.font; font.pixelSize: 18 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: qsTr("Classic recognition is always the basis, also when an AI helps. Score a script against a reference, or run the golden clips through the real pipeline: a change only becomes the default when the clips get better and none gets worse.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                    }
                }
                Step {
                    visible: page.tab === 1
                    number: "1"; title: qsTr("Cut a clip")
                    text: qsTr("In and out point, width, with or without sound: short clips for Compare and the golden list.")
                    status: qsTr("D-155 step 6.")
                }
                Step {
                    visible: page.tab === 1
                    number: "2"; title: qsTr("Compare scripts")
                    text: qsTr("A candidate against a reference on the same video: a verdict and the numbers behind it, and a label for the AI.")
                    status: qsTr("D-155 step 6.")
                }
                Step {
                    visible: page.tab === 1
                    number: "3"; title: qsTr("Golden clips")
                    text: qsTr("Runs every clip of the list, with the success rate, the quality check, the average score and correlation, and the history of runs.")
                    status: qsTr("D-155 step 6.")
                }
            }
        }
    }
}
