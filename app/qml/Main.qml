import QtQuick
import SamN
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1440; height: 900
    minimumWidth: 960; minimumHeight: 640
    visible: true
    title: "SamNPlayer 2"
    color: Theme.bgBottom
    // Test hook: SAMN_MODE=1..4 opens that mode (the numbers of the mode deck).
    property int mode: startMode
    readonly property var modeNames: deck.modes.map(m => m.name)

    // Background: near-black navy with blue / orange light ribbons in the
    // top-right and bottom-left corners (0.7 assets).
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.bgTop }
            GradientStop { position: 1; color: Theme.bgBottom }
        }
    }
    Image {
        anchors.right: parent.right; anchors.top: parent.top
        width: parent.width * 0.44; fillMode: Image.PreserveAspectFit
        source: "assets/bg-top.svg"; sourceSize.width: 1400
        asynchronous: true
    }
    Image {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        width: parent.width * 0.38; fillMode: Image.PreserveAspectFit
        source: "assets/bg-bottom.svg"; sourceSize.width: 1200
        asynchronous: true
    }

    // The one connection to the toy / stroker, shared by every mode (0.7: one
    // device connection for the whole app).
    DeviceController { id: deviceCtl }

    TopBar {
        id: top
        width: parent.width
        modeName: win.modeNames[win.mode]
        modeIcon: deck.modes[win.mode].icon
        device: deviceCtl
        onModeDeckRequested: deck.open = true
        onOpenRequested: { win.mode = 0; player.openFile() }
        onDeviceRequested: { win.mode = 0; player.showDevices() }
    }

    StackLayout {
        id: pages
        anchors.top: top.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 20; anchors.topMargin: 6
        currentIndex: win.mode
        PlayerPage { id: player; deckOpen: deck.open; device: deviceCtl }
        // Generator and Editor are one mode: the Generator is the Editor (D-137).
        GeneratorPage {
            id: generator
            deckOpen: deck.open
            playerVideoUrl: player.controller.videoUrl
            // The result opens in the Generator's Editor; "Play in the Player" hands it over (D-144).
            onReviewRequested: (path) => { win.mode = 0; player.controller.openPath(path) }
        }
        TrainingPage { id: training; deckOpen: deck.open; device: deviceCtl; autostart: trainingStart }
        NotYetPage { mode: qsTr("Scene Map"); phase: "P4" }
    }

    // Drops anywhere in the window open in the Player (0.7 files:dropped,
    // drop:script); the overlay shows while something hovers.
    DropArea {
        id: drop
        anchors.fill: parent
        onDropped: (d) => {
            if (!d.hasUrls) return
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
    Shortcut { sequence: StandardKey.Open; enabled: !deck.open; onActivated: { win.mode = 0; player.openFile() } }

    ModeDeck {
        id: deck
        anchors.fill: parent
        current: win.mode
        onChosen: (i) => win.mode = i
    }

    Shortcut { sequence: "M"; onActivated: deck.open = !deck.open }
    Shortcut { sequence: "Esc"; enabled: deck.open; onActivated: deck.open = false }
    Repeater {
        model: deck.modes.length
        delegate: Item {
            required property int index
            Shortcut { sequence: String(index + 1); enabled: deck.open; onActivated: { win.mode = index; deck.open = false } }
        }
    }

    Component.onCompleted: if (startupPaths.length > 0) player.controller.openPaths(startupPaths)
}
