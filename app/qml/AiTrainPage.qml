import QtQuick
import QtQuick.Dialogs
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
    property alias labels: labelCtl
    // With a clip open: the steps and the teacher setup instead of the editor.
    property bool showSetup: false

    LabelController { id: labelCtl }
    TrainController { id: trainCtl }
    property alias trainer: trainCtl
    FileDialog {
        id: clipDialog
        title: qsTr("Open a clip or an image to label")
        nameFilters: [qsTr("Video or image (*.mp4 *.mkv *.webm *.mov *.avi *.m4v *.png *.jpg *.jpeg)"), qsTr("All files (*)")]
        onAccepted: { page.showSetup = false; labelCtl.openVideo(selectedFile.toString()) }
    }
    FileDialog {
        id: nudenetDialog
        title: qsTr("Choose a NudeNet model (320n.onnx)")
        nameFilters: [qsTr("ONNX model (*.onnx)"), qsTr("All files (*)")]
        onAccepted: labelCtl.chooseNudeNet(selectedFile.toString())
    }

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
                text: labelCtl.hasVideo && page.tab === 0 ? labelCtl.videoName : qsTr("Preview — turn it off in Settings → General.")
                color: labelCtl.hasVideo && page.tab === 0 ? Theme.text : Theme.textMuted
                font.family: Theme.fontSoft; font.pixelSize: 12
            }
            FlowButton {
                objectName: "aiTrainSetup"
                visible: page.tab === 0 && labelCtl.hasVideo
                text: page.showSetup ? qsTr("Back to the clip") : qsTr("Teachers and steps")
                onClicked: page.showSetup = !page.showSetup
            }
            FlowButton {
                objectName: "openLabelClip"
                visible: page.tab === 0
                primary: !labelCtl.hasVideo
                text: labelCtl.hasVideo ? qsTr("Open another…") : qsTr("Open a clip or image…")
                onClicked: clipDialog.open()
            }
        }

        LabelEditor {
            objectName: "labelEditor"
            visible: page.tab === 0 && labelCtl.hasVideo && !page.showSetup
            active: visible && !page.overlayOpen
            Layout.fillWidth: true; Layout.fillHeight: true
            labels: labelCtl
        }

        Flickable {
            objectName: "aiTrainPageBody"
            visible: !(page.tab === 0 && labelCtl.hasVideo && !page.showSetup)
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
                    status: qsTr("Open a clip or image above to start. T tracks between your boxes, N jumps to the next unsure frame.")
                }
                Step {
                    visible: page.tab === 0
                    number: "2"; title: qsTr("Teachers")
                    text: qsTr("NudeNet (one click to install) and vision models on Ollama, LM Studio or a server in your network, such as a Mac with larger models, propose boxes. Where several agree, the proposal is offered; you confirm it in the label editor.")
                    status: qsTr("NudeNet on this computer and vision models on a server: below. In the label editor, Shift+A asks all of them; Teachers through the clip asks them every 2 s. What all see is a plain proposal, what only some see is marked uncertain (N jumps there).")
                }
                // Which teacher answers "Ask the teacher" (A).
                RowLayout {
                    objectName: "teacherKindRow"
                    visible: page.tab === 0
                    Layout.fillWidth: true; spacing: 8
                    Text { textFormat: Text.PlainText; text: qsTr("Ask the teacher (A) asks:"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                    FlowButton {
                        objectName: "kindNudeNet"
                        text: qsTr("NudeNet here")
                        checked: labelCtl.teacherKind === "nudenet"
                        enabled: labelCtl.localTeacherAvailable
                        onClicked: labelCtl.teacherKind = "nudenet"
                    }
                    FlowButton {
                        objectName: "kindServer"
                        text: qsTr("The model server")
                        checked: labelCtl.teacherKind === "server"
                        onClicked: labelCtl.teacherKind = "server"
                    }
                    Item { Layout.fillWidth: true }
                }
                // NudeNet on this computer (D-155 step 4): ONNX Runtime, one click.
                GlassCard {
                    objectName: "nudenetCard"
                    visible: page.tab === 0
                    Layout.fillWidth: true
                    implicitHeight: nudenetCol.implicitHeight + 28
                    ColumnLayout {
                        id: nudenetCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("NudeNet on this computer"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: labelCtl.localTeacherAvailable
                                  ? qsTr("A small detector of body parts that runs here, no server, no upload. Its weights are not part of SamNPlayer (their licence is unclear): one click fetches NudeNet 3.4.2 from PyPI (about 11 MB) and checks it against a fixed checksum. It proposes penis, vagina, breast and face.")
                                  : qsTr("This build cannot run models itself (it has no ONNX Runtime). Use a model server below.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            visible: labelCtl.localTeacherAvailable
                            FlowButton {
                                objectName: "nudenetDownload"
                                visible: labelCtl.modelDownload < 0
                                primary: labelCtl.nudenetPath === ""
                                text: labelCtl.nudenetPath === "" ? qsTr("Download NudeNet") : qsTr("Download again")
                                icon: "download"
                                enabled: !labelCtl.teacherBusy
                                onClicked: labelCtl.downloadNudeNet()
                            }
                            Text {
                                objectName: "nudenetProgress"
                                visible: labelCtl.modelDownload >= 0
                                textFormat: Text.PlainText
                                text: qsTr("Fetching NudeNet … %1 %").arg(labelCtl.modelDownload)
                                color: Theme.cyan; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                            FlowButton {
                                objectName: "nudenetStop"
                                visible: labelCtl.modelDownload >= 0
                                text: qsTr("Stop")
                                onClicked: labelCtl.cancelModelDownload()
                            }
                            FlowButton {
                                objectName: "nudenetChoose"
                                visible: labelCtl.modelDownload < 0
                                text: qsTr("Choose a file…")
                                enabled: !labelCtl.teacherBusy
                                onClicked: nudenetDialog.open()
                            }
                            Text {
                                objectName: "nudenetState"
                                Layout.fillWidth: true
                                textFormat: Text.PlainText; elide: Text.ElideMiddle
                                text: labelCtl.nudenetPath !== "" ? qsTr("Ready") : qsTr("Not fetched yet")
                                color: labelCtl.nudenetPath !== "" ? Theme.cyan : Theme.textDim
                                font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                    }
                }
                // The teacher on a model server (D-155 step 4).
                GlassCard {
                    objectName: "teacherCard"
                    visible: page.tab === 0
                    Layout.fillWidth: true
                    implicitHeight: teacherCol.implicitHeight + 28
                    ColumnLayout {
                        id: teacherCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Teacher on a model server"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            text: qsTr("Any OpenAI-compatible address with a vision model: Ollama (http://<host>:11434/v1), LM Studio (http://<host>:1234/v1) or another server in your network. The frame goes only to this address.")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowTextField {
                                objectName: "teacherUrl"
                                Layout.fillWidth: true
                                maxLength: 200
                                placeholder: "http://192.168.1.20:11434/v1"
                                text: labelCtl.teacherUrl
                                onAccepted: labelCtl.teacherUrl = text
                                onTextChanged: if (text !== labelCtl.teacherUrl) labelCtl.teacherUrl = text
                            }
                            FlowButton {
                                objectName: "teacherTest"
                                text: qsTr("Test connection")
                                enabled: !labelCtl.teacherBusy
                                onClicked: labelCtl.testTeacher()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowCombo {
                                objectName: "teacherModel"
                                Layout.preferredWidth: 260; height: 32
                                model: labelCtl.teacherModels
                                enabled: labelCtl.teacherModels.length > 0
                                displayText: labelCtl.teacherModel !== "" ? labelCtl.teacherModel : qsTr("Model (test the connection)")
                                onActivated: (i) => labelCtl.teacherModel = labelCtl.teacherModels[i]
                            }
                            FlowTextField {
                                objectName: "teacherKey"
                                Layout.fillWidth: true
                                secret: true; maxLength: 200
                                placeholder: qsTr("Key (only if the server asks for one)")
                                text: labelCtl.teacherKey
                                onTextChanged: if (text !== labelCtl.teacherKey) labelCtl.teacherKey = text
                            }
                        }
                        Text {
                            objectName: "teacherStatus"
                            textFormat: Text.PlainText
                            Layout.fillWidth: true; wrapMode: Text.WordWrap
                            visible: text !== ""
                            text: labelCtl.teacherStatus
                            color: Theme.cyan; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                    }
                }
                Step {
                    visible: page.tab === 0
                    number: "3"; title: qsTr("Train our model")
                    text: qsTr("In the label editor, Add to training set turns every N-th frame of the confirmed stretches into samples. RF-DETR learns from them, whole clips held out to check it, and is kept as one ONNX file. Training runs in a Python of the app's own, set up with one click in its AI folder: nothing is installed on the system.")
                    status: trainCtl.studentPath !== "" ? qsTr("A student is ready: %1.").arg(trainCtl.studentClasses.join(", "))
                                                       : qsTr("No student yet.")
                }
                // D-155 step 5: the environment, the training set and the run.
                GlassCard {
                    objectName: "trainCard"
                    visible: page.tab === 0
                    Layout.fillWidth: true
                    implicitHeight: trainCol.implicitHeight + 28
                    ColumnLayout {
                        id: trainCol
                        anchors.fill: parent; anchors.margins: 14; spacing: 8
                        Text { textFormat: Text.PlainText; text: qsTr("Train the student"); color: Theme.text; font.family: Theme.font; font.pixelSize: 15 }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Text { textFormat: Text.PlainText; text: qsTr("Runs on"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                            FlowCombo {
                                id: deviceBox
                                objectName: "trainDevice"
                                Layout.preferredWidth: 220; height: 32
                                readonly property var values: ["auto", "cuda", "cpu"]
                                model: [qsTr("Automatic (NVIDIA if there)"), qsTr("NVIDIA card (CUDA)"), qsTr("Processor only")]
                                currentIndex: Math.max(0, values.indexOf(trainCtl.device))
                                enabled: !trainCtl.settingUp && !trainCtl.training
                                onActivated: (i) => trainCtl.device = values[i]
                            }
                            Text { textFormat: Text.PlainText; text: qsTr("Epochs"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                            FlowSlider {
                                objectName: "trainEpochs"
                                Layout.preferredWidth: 140
                                from: 5; to: 200; value: trainCtl.epochs
                                onMoved: (v) => trainCtl.epochs = Math.round(v)
                            }
                            Text { textFormat: Text.PlainText; text: trainCtl.epochs; color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12 }
                            Item { Layout.fillWidth: true }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowButton {
                                objectName: "trainSetUp"
                                visible: !trainCtl.settingUp
                                primary: !trainCtl.envReady
                                text: trainCtl.envReady ? qsTr("Set up again") : qsTr("Set up training (one click)")
                                icon: "download"
                                enabled: !trainCtl.training
                                onClicked: trainCtl.setUpEnvironment()
                            }
                            FlowButton {
                                objectName: "trainSetUpStop"
                                visible: trainCtl.settingUp
                                text: qsTr("Stop")
                                onClicked: trainCtl.cancelSetup()
                            }
                            Text {
                                objectName: "trainEnvStatus"
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                text: trainCtl.envStatus !== "" ? trainCtl.envStatus
                                      : trainCtl.envReady ? qsTr("Ready.")
                                      : qsTr("About 0.3 GB for the processor, about 3 GB with an NVIDIA card; fetched once.")
                                color: trainCtl.envReady ? Theme.cyan : Theme.textDim
                                font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: trainCtl.datasets.length === 0 ? qsTr("The training set is empty: Add to training set in the label editor.")
                                                                 : qsTr("Training set:")
                            color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
                        }
                        Flow {
                            Layout.fillWidth: true; spacing: 6
                            Repeater {
                                model: trainCtl.datasets
                                delegate: Rectangle {
                                    required property var modelData
                                    objectName: "trainSet_" + modelData.name
                                    height: 26; radius: 13
                                    width: setLabel.implicitWidth + 40
                                    color: Qt.rgba(1, 1, 1, 0.06)
                                    Text {
                                        id: setLabel
                                        anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                        textFormat: Text.PlainText
                                        text: qsTr("%1 · %2 samples").arg(modelData.name).arg(modelData.samples)
                                        color: Theme.text; font.family: Theme.fontSoft; font.pixelSize: 12
                                    }
                                    Text {
                                        anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter
                                        text: "×"; color: Theme.textDim; font.pixelSize: 14
                                        MouseArea {
                                            anchors.fill: parent; anchors.margins: -4
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: !trainCtl.training
                                            onClicked: trainCtl.removeDataset(modelData.name)
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            FlowButton {
                                objectName: "trainStart"
                                primary: true
                                text: trainCtl.training ? qsTr("Stop (%1 %)").arg(trainCtl.trainProgress) : qsTr("Train")
                                icon: "play"
                                enabled: trainCtl.training || (trainCtl.envReady && !trainCtl.settingUp && trainCtl.datasets.length > 0)
                                onClicked: trainCtl.training ? trainCtl.stop() : trainCtl.train()
                            }
                            Text {
                                objectName: "trainStatus"
                                Layout.fillWidth: true; wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                visible: text !== ""
                                text: trainCtl.trainStatus
                                color: Theme.cyan; font.family: Theme.fontSoft; font.pixelSize: 12
                            }
                        }
                    }
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
