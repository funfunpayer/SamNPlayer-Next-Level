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
    property int mode: 0
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

    TopBar {
        id: top
        width: parent.width
        modeName: win.modeNames[win.mode]
        modeIcon: deck.modes[win.mode].icon
        onModeDeckRequested: deck.open = true
    }

    StackLayout {
        id: pages
        anchors.top: top.bottom; anchors.bottom: parent.bottom
        anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 20; anchors.topMargin: 6
        currentIndex: win.mode
        PlayerPage { id: player; deckOpen: deck.open }
        // Generator and Editor are one mode: the Generator is the Editor (D-137).
        NotYetPage { mode: qsTr("Generator"); phase: "P3" }
        NotYetPage { mode: qsTr("Training"); phase: "P4" }
        NotYetPage { mode: qsTr("Scene Map"); phase: "P4" }
    }

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

    Component.onCompleted: if (startupPath !== "") player.controller.openPath(startupPath)
}
