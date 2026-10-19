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
                            text: qsTr("Free tag boxes → assign a body-part label → sample → review → train locally. Draw up to 12 boxes per image and give each a label; the same label may be used more than once (nipples need two boxes). “Use for training” tracks every labelled box through the clip. Everything stays on this computer.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
                        }
                        Text { textFormat: Text.PlainText; text: qsTr("From marks to a good script"); color: Theme.text; font.family: Theme.font; font.pixelSize: 14; topPadding: 4 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: qsTr("1. Train here: marks → Use for training → discard bad samples → train. The result is a tip-finding model.\n2. Then Create: the Generator's Find tip asks the model for the box. It only suggests.\n3. Check the box; never apply it blindly.\n4. Create: classic tracking writes the script. The AI does not track by itself.\n5. Play and review.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13; lineHeight: 1.2
                        }
                    }
                }
                Step {
                    visible: page.tab === 0
                    number: "1"; title: qsTr("Collect")
                    text: qsTr("Open a video or an image, go to a moment, draw up to 12 named boxes and track them through the clip. Every N-th frame becomes a sample; the sound can be kept too.")
                    status: qsTr("Comes next (D-155 step 2).")
                }
                Step {
                    visible: page.tab === 0
                    number: "2"; title: qsTr("Review")
                    text: qsTr("The new samples with their boxes. Correct a box, relabel it or discard the sample: tracking can be wrong on real footage, so review is part of the flow.")
                    status: qsTr("D-155 step 3.")
                }
                Step {
                    visible: page.tab === 0
                    number: "3"; title: qsTr("Dataset")
                    text: qsTr("How many boxes of each label are in training and validation, and whether the data is ready to train. The learned generation profile lives here too.")
                    status: qsTr("D-155 step 4.")
                }
                Step {
                    visible: page.tab === 0
                    number: "4"; title: qsTr("Train")
                    text: qsTr("Epochs and device (automatic, NVIDIA CUDA, DirectML, Apple MPS, CPU). Training runs in your Python; the result is an ONNX model the Generator uses to find the tip.")
                    status: qsTr("D-155 step 5.")
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
