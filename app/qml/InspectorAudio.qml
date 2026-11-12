import QtQuick
import QtQuick.Dialogs
import SamN

Column {
    id: root
    required property var ctrl
    required property var device
    required property Item rootPanel
    width: parent ? parent.width : 280
    spacing: 10
    InspectorCard {
        title: qsTr("BPM")
        help: "playback.bpm"
        Row {
            spacing: 8
            FlowTextField {
                objectName: "bpmField"
                width: 90
                placeholder: ctrl.bpm > 0 ? String(ctrl.bpm)
                           : ctrl.effectiveBpm > 0 ? qsTr("auto %1").arg(ctrl.effectiveBpm)
                                                       : qsTr("20–240")
                onAccepted: ctrl.bpm = parseFloat(text) || 0
            }
            FlowButton {
                objectName: "bpmGrid"
                text: qsTr("BPM grid")
                checked: ctrl.bpmGrid
                onClicked: ctrl.bpmGrid = !ctrl.bpmGrid
            }
            HelpChip { key: "pb-bpm-grid"; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 8
            FlowButton {
                objectName: "bpmFromStroke"
                text: qsTr("Guess from stroke")
                enabled: ctrl.hasScript
                onClicked: ctrl.guessBpmFromStroke()
            }
            FlowButton {
                objectName: "bpmFromAudio"
                text: qsTr("From audio")
                enabled: ctrl.videoName !== ""
                onClicked: ctrl.guessBpmFromAudio()
            }
            HelpChip { key: "pb-bpm-auto"; anchors.verticalCenter: parent.verticalCenter }
        }
        InspectorNote { text: qsTr("Beat lines on the curve (every 4th is a bar). Display only. Leave BPM blank to use the stroke's own tempo when the grid is on. From audio measures the linked video's sound (first 10 min; the beat when it's clear).") }
        InspectorNote { objectName: "bpmNote"; visible: ctrl.toolNote !== "" && (ctrl.toolNote.indexOf("BPM") >= 0 || ctrl.toolNote.indexOf("audio") >= 0 || ctrl.toolNote.indexOf("sound") >= 0); text: ctrl.toolNote }
    }
    InspectorCard {
        title: qsTr("Sound Curve")
        help: "pb-aa-write"
        InspectorNote { text: qsTr("Write the video's loudness into the script as the Vibration curve (smoothed). Playback then uses axes mode. Undo restores the previous vibration for this script.") }
        Row {
            spacing: 8
            FlowButton {
                objectName: "soundCurveWriteInspector"
                text: qsTr("Sound curve")
                enabled: ctrl.hasScript && ctrl.videoName !== ""
                onClicked: ctrl.proposeSoundCurve()
            }
            FlowButton {
                objectName: "soundCurveUndoInspector"
                text: qsTr("Undo")
                visible: ctrl.canUndoSoundCurve
                onClicked: ctrl.undoSoundCurve()
            }
        }
        InspectorNote {
            objectName: "soundCurveNote"
            visible: ctrl.toolNote !== "" && ctrl.toolNote.indexOf("Sound") >= 0
            text: ctrl.toolNote
        }
    }
    InspectorCard {
        title: qsTr("Audio Vibration")
        help: "pb-audio-vibration"
        FlowButton {
            objectName: "pb-audio-vibration"
            text: qsTr("Audio vibration")
            checked: ctrl.audioVibration
            onClicked: ctrl.audioVibration = !ctrl.audioVibration
        }
        InspectorNote {
            text: ctrl.audioVibration
                  ? (ctrl.audioVibrationReady
                     ? qsTr("On: at the next Play start, vibration follows the video's sound. Suction stays on the script. Files are not changed.")
                     : qsTr("On: reading the sound… Play keeps script vibration until the envelope is ready."))
                  : qsTr("Off (default). For a denser bake into the file, use the Editor (“Vibration from the sound”).")
        }
        FlowButton { objectName: "editForSound"; text: qsTr("Open in the Editor"); enabled: ctrl.hasScript; onClicked: rootPanel.editRequested(false) }
    }
}
