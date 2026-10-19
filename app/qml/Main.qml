import QtQuick
import SamN
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1440; height: 900
    minimumWidth: 960; minimumHeight: 640
    visible: true
    title: "SamNPlayer Next"
    color: Theme.bgBottom
    // Test hook: SAMN_MODE=1..3 opens that mode (the order of the tabs).
    property int mode: startMode
    readonly property var modeNames: top.modes.map(m => m.name)

    // Background: near-black navy with blue / orange light ribbons in the
    // top-right and bottom-left corners (0.7 assets).
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.bgTop }
            GradientStop { position: 1; color: Theme.bgBottom }
        }
    }
    // The silk bands of the Flow look in their corners, large and slowly
    // drifting: blue from the top right, orange from the bottom left, behind
    // every mode (Owner, 19 Oct: as strong as on the Training stage).
    Image {
        id: silkTop
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: -parent.width * 0.04 + driftX; anchors.topMargin: -parent.height * 0.03 + driftY
        width: parent.width * 0.66; fillMode: Image.PreserveAspectFit
        source: "assets/bg-top.svg"; sourceSize.width: 2000
        asynchronous: true
        property real driftX: 0
        property real driftY: 0
        SequentialAnimation on driftX {
            running: win.visible; loops: Animation.Infinite
            NumberAnimation { to: -win.width * 0.025; duration: 32000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 32000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on driftY {
            running: win.visible; loops: Animation.Infinite
            NumberAnimation { to: win.height * 0.02; duration: 32000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 32000; easing.type: Easing.InOutSine }
        }
    }
    Image {
        id: silkBottom
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.leftMargin: -parent.width * 0.03 + driftX; anchors.bottomMargin: -parent.height * 0.02 + driftY
        width: parent.width * 0.58; fillMode: Image.PreserveAspectFit
        source: "assets/bg-bottom.svg"; sourceSize.width: 2000
        asynchronous: true
        property real driftX: 0
        property real driftY: 0
        SequentialAnimation on driftX {
            running: win.visible; loops: Animation.Infinite
            NumberAnimation { to: win.width * 0.03; duration: 40000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 40000; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on driftY {
            running: win.visible; loops: Animation.Infinite
            NumberAnimation { to: win.height * 0.02; duration: 40000; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0; duration: 40000; easing.type: Easing.InOutSine }
        }
    }

    // The one connection to the toy / stroker, shared by every mode (0.7: one
    // device connection for the whole app).
    DeviceController { id: deviceCtl }
    // Check for updates, check only (D-147); the startup check is quiet.
    UpdateChecker { id: updates }

    TopBar {
        id: top
        width: parent.width
        currentMode: win.mode
        onModeChosen: (i) => win.mode = i
        device: deviceCtl
        onOpenRequested: { win.mode = 0; player.openFile() }
        onDeviceRequested: { win.mode = 0; player.showDevices() }
        onSettingsRequested: settingsPage.open = true
    }

    StackLayout {
        id: pages
        anchors.top: top.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 20; anchors.topMargin: 6
        currentIndex: win.mode
        PlayerPage {
            id: player; overlayOpen: settingsPage.open; device: deviceCtl
            // 0.7: "Create script" goes to the Generator with this video.
            onCreateRequested: (video, start) => { win.mode = 1; generator.createFor(video, start) }
        }
        // Generator and Editor are one mode: the Generator is the Editor (D-137).
        GeneratorPage {
            id: generator
            overlayOpen: settingsPage.open
            playerVideoUrl: player.controller.videoUrl
            // The result opens in the Generator's Editor; "Play in the Player" hands it over (D-144).
            onReviewRequested: (path) => { win.mode = 0; player.controller.openPath(path) }
        }
        TrainingPage { id: training; overlayOpen: settingsPage.open; device: deviceCtl; autostart: trainingStart }
    }

    // Drops anywhere in the window open in the Player (0.7 files:dropped,
    // drop:script); the overlay shows while something hovers.
    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: (d) => {
            if (!d.hasUrls) return
            // 0.7: one video dropped in the Generator stays there.
            if (win.mode === 1 && d.urls.length === 1 && player.controller.isVideoFile(d.urls[0].toString())) {
                generator.createFor(d.urls[0], false)
                return
            }
            win.mode = 0
            player.controller.openPaths(d.urls.map(u => u.toString()))
        }
    }
    Rectangle {
        objectName: "dropOverlay"
        anchors.fill: parent
        visible: drop.containsDrag
        color: Qt.rgba(0, 20 / 255, 45 / 255, 0.72)
        border.color: Theme.cyan; border.width: 2
        Text {
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: qsTr("Drop a video or script here")
            color: "white"; font.family: Theme.font; font.pixelSize: 22
        }
    }
    // Ctrl+O from every mode opens in the Player (0.7 top-bar Open).
    Shortcut { sequence: StandardKey.Open; enabled: !settingsPage.open; onActivated: { win.mode = 0; player.openFile() } }

    // 0.7 startup update banner: the new version, its notes, the download
    // page; × hides it until the next start.
    GlassCard {
        id: updateBanner
        objectName: "updateBanner"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: top.bottom; anchors.topMargin: 4
        width: Math.min(parent.width - 40, 640)
        implicitHeight: bannerRow.implicitHeight + 20
        visible: opacity > 0
        opacity: updates.bannerVisible && !settingsPage.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }
        // Opaque under the glass, as the Settings card.
        Rectangle { anchors.fill: parent; radius: Theme.radius; color: Theme.bgBottom; z: -1 }
        RowLayout {
            id: bannerRow
            anchors.fill: parent; anchors.margins: 10; anchors.leftMargin: 16; spacing: 10
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Text {
                    objectName: "updateBannerText"
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: qsTr("SamNPlayer %1 is available (you have %2).").arg(updates.latestTag).arg(updates.currentVersion)
                    color: Theme.text; font.family: Theme.font; font.pixelSize: 14
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true; elide: Text.ElideRight
                    visible: text !== ""
                    text: updates.notesPreview
                    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
                }
            }
            FlowButton { objectName: "updateBannerOpen"; primary: true; text: qsTr("Download page"); onClicked: updates.openReleasePage() }
            IconButton { objectName: "updateBannerClose"; icon: "close"; tip: qsTr("Hide"); onClicked: updates.dismissBanner() }
        }
    }


    // Settings over the current mode (D-146); Esc closes it.
    SettingsPage {
        id: settingsPage
        anchors.fill: parent
        player: player.controller
        device: deviceCtl
        updates: updates
    }
    Shortcut { sequence: "Ctrl+,"; onActivated: settingsPage.open = !settingsPage.open }
    Shortcut { sequence: "Esc"; enabled: settingsPage.open; onActivated: settingsPage.open = false }

    // Ctrl+1…3: the modes, as the tabs (D-150; replaces the M deck).
    Repeater {
        model: top.modes.length
        delegate: Item {
            required property int index
            Shortcut { sequence: "Ctrl+" + (index + 1); onActivated: win.mode = index }
        }
    }

    Component.onCompleted: {
        if (startupPaths.length > 0) player.controller.openPaths(startupPaths)
        updates.checkIfEnabled()
    }
}
