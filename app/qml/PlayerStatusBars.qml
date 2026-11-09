import QtQuick
import SamN

// Soft link warning, then the hard error bar. Child ids stay inside this
// component, so the root owns the vertical space the page used to take from
// errorBar.bottom (8px above a bar that is showing, none when it is not).
Item {
    id: root
    width: parent.width
    implicitHeight: errorBar.y + errorBar.height
    height: implicitHeight

    required property PlayerController ctrl
    property bool noPicture
    // ClipPrepController: cutting / fetching / progress drive both buttons.
    property var convertCtl

    signal convertRequested()
    signal dismissLinkWarningRequested()

    // Soft probe-on-link warning (before play fails). Distinct from the
    // hard error bar; dismissed on first picture or by the user.
    Rectangle {
        id: linkWarnBar
        objectName: "videoLinkWarning"
        readonly property bool show: ctrl.videoLinkWarning !== "" && ctrl.lastError === ""
        // Was anchors.top: header.bottom; topMargin: show ? 8 : 0. The root
        // sits on the header, so the margin lives inside.
        y: show ? 8 : 0
        width: parent.width
        height: show ? Math.max(linkWarnText.implicitHeight, 36) + 16 : 0
        visible: height > 0; clip: true
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 0.72, 0.25, 0.12); border.color: Qt.rgba(1, 0.75, 0.3, 0.45)
        Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Text {
            id: linkWarnText
            textFormat: Text.PlainText
            anchors.verticalCenter: parent.verticalCenter
            x: 14
            width: parent.width - 28 - (ctrl.videoLinkSuggestPlayable ? linkMakePlayable.width + 12 : 0)
                                       - (linkDismiss.width + 12)
            text: ctrl.videoLinkWarning; wrapMode: Text.Wrap
            color: "#ffe8c0"; font.family: Theme.fontSoft; font.pixelSize: 13
        }
        FlowButton {
            id: linkMakePlayable
            objectName: "linkMakePlayable"
            anchors.right: linkDismiss.left; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: ctrl.videoLinkSuggestPlayable
            primary: true
            enabled: !convertCtl.cutting && !convertCtl.fetching
            text: convertCtl.cutting ? (qsTr("%1 %").arg(convertCtl.progress))
                  : convertCtl.fetching ? qsTr("Getting ffmpeg…")
                  : qsTr("Make playable")
            onClicked: root.convertRequested()
        }
        FlowButton {
            id: linkDismiss
            objectName: "dismissLinkWarning"
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Dismiss")
            onClicked: root.dismissLinkWarningRequested()
        }
    }

    // Errors stay visible until the next successful open. Make playable
    // appears for media failures and for no-picture (same H.264 convert).
    Rectangle {
        id: errorBar
        readonly property bool offerPlayable: ctrl.videoName !== "" && ctrl.lastError !== ""
                                             && (ctrl.mediaError || root.noPicture
                                                 || ctrl.lastError.indexOf("Make playable") >= 0)
        // Was: linkWarnBar.visible ? linkWarnBar.bottom : header.bottom,
        // plus 8px when an error is showing.
        y: (linkWarnBar.visible ? linkWarnBar.y + linkWarnBar.height : 0) + (ctrl.lastError !== "" ? 8 : 0)
        width: parent.width
        height: ctrl.lastError !== "" ? Math.max(errText.implicitHeight, offerPlayable ? 36 : 0) + 16 : 0
        visible: height > 0; clip: true
        radius: Theme.radiusSmall
        color: Qt.rgba(1, 0.35, 0.3, 0.12); border.color: Qt.rgba(1, 0.4, 0.35, 0.5)
        Behavior on height { NumberAnimation { duration: Theme.animNormal; easing.type: Easing.OutCubic } }
        Text {
            textFormat: Text.PlainText
            id: errText
            anchors.verticalCenter: parent.verticalCenter
            x: 14
            width: parent.width - 28 - (errorBar.offerPlayable ? makePlayableBtn.width + 16 : 0)
            text: ctrl.lastError; wrapMode: Text.Wrap
            color: "#ffd0c8"; font.family: Theme.fontSoft; font.pixelSize: 13
        }
        FlowButton {
            id: makePlayableBtn
            objectName: "makePlayable"
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            visible: errorBar.offerPlayable
            primary: true
            enabled: !convertCtl.cutting && !convertCtl.fetching
            text: convertCtl.cutting ? (qsTr("%1 %").arg(convertCtl.progress))
                  : convertCtl.fetching ? qsTr("Getting ffmpeg…")
                  : qsTr("Make playable")
            onClicked: root.convertRequested()
        }
    }
}
