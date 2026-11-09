import QtQuick
import SamN

// Player header: what is open, the script variant, and Open.
// Dialogs and the theatre / fullscreen flags stay on PlayerPage; this item
// only asks.
Item {
    id: root
    width: parent.width
    height: 40

    required property PlayerController ctrl
    property bool empty
    property bool theatre
    property bool theatreLanes
    property bool fullStage

    signal openRequested()
    signal videoPickRequested()
    signal bookmarkRequested()
    signal snapshotRequested()
    signal theatreToggled()
    signal theatreLanesToggled()
    signal fullStageToggled()

    Text {
        textFormat: Text.PlainText
        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
        anchors.right: variantBox.visible ? variantBox.left : videoBar.visible ? videoBar.left : openBtn.left
        anchors.rightMargin: 12
        text: ctrl.hasScript ? ctrl.scriptName + (ctrl.videoName !== "" ? "  ·  " + ctrl.videoName : "")
                             : ctrl.videoName !== "" ? ctrl.videoName : qsTr("Open a video or a script")
        color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 14
        elide: Text.ElideMiddle
    }
    FlowCombo {
        id: variantBox
        anchors.right: videoBar.visible ? videoBar.left : openBtn.left; anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 240
        visible: ctrl.variants.length > 1
        objectName: "variantBox"
        // 0.7: a script that is not one of the variants shows first as itself.
        readonly property bool other: ctrl.variants.findIndex((v) => v.path === ctrl.scriptPath) < 0
        model: (other ? [{ display: ctrl.scriptName !== "" ? ctrl.scriptName : qsTr("Other script"), path: "" }] : []).concat(ctrl.variants)
        textRole: "display"
        currentIndex: other ? 0 : ctrl.variants.findIndex((v) => v.path === ctrl.scriptPath)
        onActivated: (i) => { if (!other || i > 0) ctrl.chooseVariant(other ? i - 1 : i) }
    }
    // Video bar (0.7 v1): link a video, bookmark, snapshot.
    Row {
        id: videoBar
        anchors.right: fsBtn.visible ? fsBtn.left : openBtn.left; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
        spacing: 8
        visible: ctrl.hasScript
        FlowButton {
            icon: "video"
            text: ctrl.videoName !== "" ? ctrl.videoName : qsTr("Select a video file…")
            onClicked: root.videoPickRequested()
        }
        IconButton {
            width: 36; height: 36; icon: "bookmark"; tip: qsTr("Add a bookmark at the current time")
            onClicked: root.bookmarkRequested()
        }
        IconButton {
            width: 36; height: 36; icon: "snapshot"; tip: qsTr("Save the current video frame as PNG")
            visible: ctrl.videoName !== ""
            onClicked: root.snapshotRequested()
        }
    }
    IconButton {
        id: theatreLanesBtn
        objectName: "theatreLanesButton"
        anchors.right: theatreBtn.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36; icon: "waveform"
        visible: root.theatre && !root.empty
        opacity: root.theatreLanes ? 1 : 0.45
        tip: root.theatreLanes ? qsTr("Hide the curve and waveform") : qsTr("Show the curve and waveform")
        onClicked: root.theatreLanesToggled()
    }
    IconButton {
        id: theatreBtn
        objectName: "theatreButton"
        anchors.right: fsBtn.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36; icon: root.theatre ? "minimize" : "video"
        visible: !root.empty
        tip: root.theatre ? qsTr("Leave theatre mode (T)") : qsTr("Theatre mode: the video large, panels away (T)")
        onClicked: root.theatreToggled()
    }
    IconButton {
        id: fsBtn
        anchors.right: openBtn.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
        width: 36; height: 36; icon: root.fullStage ? "minimize" : "fullscreen"
        tip: qsTr("Fullscreen (double-click the video)")
        visible: ctrl.videoName !== ""
        onClicked: root.fullStageToggled()
    }
    FlowButton {
        id: openBtn
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        primary: !root.empty; icon: "folder"; text: qsTr("Open…")
        onClicked: root.openRequested()
    }
}
