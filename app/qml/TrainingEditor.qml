import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import SamN

// Script editor (0.7 "Script editor - build your own multi-axis pattern"): a list of
// phases, each with a Vibration and a Suction axis. An axis that is switched off leaves
// that channel untouched. Saves as a program under My programs.
Item {
    id: root
    required property TrainingController controller
    property bool open: false
    // The script being edited, in the JSON shape of the controller.
    property var draft: blank()
    property string original: ""      // name of the saved program being edited, "" = new
    property string status: ""
    property bool statusOk: false
    visible: open
    z: 20

    function curve() { return { startLevel: 0.2, peakLevel: 0.6, endLevel: 0.2, rampUpMs: 3000, holdMs: 2000, rampDownMs: 3000, randomJitterFraction: 0 } }
    function phase() { return { name: qsTr("Phase"), repeatCycles: 3, restMs: 3000, vibration: curve() } }
    function blank() { return { name: "", description: "", phases: [phase()], progressionPerCycle: 0 } }
    function show(name) {
        const loaded = name ? controller.scriptForEditing(name) : null
        draft = loaded && loaded.name ? loaded : blank()
        original = loaded && loaded.name ? loaded.name : ""
        status = ""
        open = true
        validate.restart()
    }
    function edit(fn) {
        const d = JSON.parse(JSON.stringify(draft))
        fn(d)
        draft = d
        validate.restart()
    }
    function move(i, by) {
        edit(d => { const p = d.phases.splice(i, 1)[0]; d.phases.splice(i + by, 0, p) })
    }
    function check() {
        const r = controller.previewDraft(draft)
        statusOk = !r.error
        status = r.error ? qsTr("Not valid yet: %1").arg(r.error)
                         : qsTr("Valid: %1, %2").arg(r.phases === 1 ? qsTr("1 phase") : qsTr("%1 phases").arg(r.phases)).arg(clock(Math.round(r.totalMs / 1000)))
    }
    function clock(sec) {
        const m = Math.floor(sec / 60), s = sec % 60
        return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
    }
    Timer { id: validate; interval: 250; onTriggered: root.check() }

    // Dim the page behind.
    Rectangle { anchors.fill: parent; color: "#aa010a14"; MouseArea { anchors.fill: parent } }

    // The card itself is see-through: put a solid panel under it so the page does not bleed through.
    Rectangle {
        anchors.fill: card; radius: Theme.radius; color: "#07142a"
    }
    GlassCard {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 860); height: Math.min(parent.height - 40, 700)
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 18
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; text: qsTr("Script editor"); color: Theme.text; font.family: Theme.font; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
                IconButton { icon: "close"; width: 34; height: 34; tip: qsTr("Close"); onClicked: root.open = false }
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: qsTr("A script is a list of phases. Each phase has two independent axes, Vibration and Suction, that can ramp up, hold and ramp down on their own timing. Switch an axis off to keep that channel untouched (it stays at whatever level the previous phase left it).")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { textFormat: Text.PlainText; text: qsTr("Load existing"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13 }
                FlowCombo {
                    Layout.preferredWidth: 220
                    model: [qsTr("-- new script --")].concat(root.controller.myPrograms.map(p => p.name))
                    currentIndex: Math.max(0, root.controller.myPrograms.findIndex(p => p.name === root.original) + 1)
                    onActivated: (i) => root.show(i === 0 ? "" : currentText)
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { textFormat: Text.PlainText; text: qsTr("Name"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13; Layout.preferredWidth: 80 }
                FlowTextField { Layout.fillWidth: true; text: root.draft.name; onTextChanged: if (text !== root.draft.name) root.edit(d => d.name = text) }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                Text { textFormat: Text.PlainText; text: qsTr("Description"); color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13; Layout.preferredWidth: 80 }
                FlowTextField { Layout.fillWidth: true; maxLength: 200; text: root.draft.description; onTextChanged: if (text !== root.draft.description) root.edit(d => d.description = text) }
            }
            Flickable {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: width; contentHeight: phases.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                Column {
                    id: phases
                    width: parent.width - 12
                    spacing: 8
                    Repeater {
                        model: root.draft.phases.length
                        delegate: Rectangle {
                            id: card
                            required property int index
                            readonly property var ph: root.draft.phases[index]
                            width: phases.width; height: body.implicitHeight + 20; radius: 12
                            color: Qt.rgba(1, 1, 1, 0.03); border.color: Theme.cardBorder
                            Column {
                                id: body
                                x: 10; y: 10; width: parent.width - 20; spacing: 8
                                RowLayout {
                                    width: parent.width; spacing: 8
                                    Text { textFormat: Text.PlainText; text: card.index + 1; color: Theme.cyan; font.family: Theme.font; font.pixelSize: 14; Layout.preferredWidth: 18 }
                                    FlowTextField { Layout.fillWidth: true; text: card.ph.name; onTextChanged: if (text !== card.ph.name) root.edit(d => d.phases[card.index].name = text) }
                                    NumField { label: qsTr("Repeats"); value: card.ph.repeatCycles; from: 1; to: 99; decimals: 0; onEdited: (v) => root.edit(d => d.phases[card.index].repeatCycles = Math.round(v)) }
                                    NumField { label: qsTr("Rest ms"); value: card.ph.restMs; from: 0; to: 60000; decimals: 0; onEdited: (v) => root.edit(d => d.phases[card.index].restMs = Math.round(v)) }
                                    IconButton { icon: "chevronDown"; rotation: 180; width: 28; height: 28; enabled: card.index > 0; tip: qsTr("Move up"); onClicked: root.move(card.index, -1) }
                                    IconButton { icon: "chevronDown"; width: 28; height: 28; enabled: card.index < root.draft.phases.length - 1; tip: qsTr("Move down"); onClicked: root.move(card.index, 1) }
                                    IconButton { icon: "layers"; width: 28; height: 28; tip: qsTr("Duplicate"); onClicked: root.edit(d => { const c = JSON.parse(JSON.stringify(d.phases[card.index])); c.name += qsTr(" copy"); d.phases.splice(card.index + 1, 0, c) }) }
                                    IconButton { icon: "trash"; width: 28; height: 28; enabled: root.draft.phases.length > 1; tip: qsTr("Delete phase"); onClicked: root.edit(d => d.phases.splice(card.index, 1)) }
                                }
                                Repeater {
                                    model: ["vibration", "suction"]
                                    delegate: Row {
                                        id: axis
                                        required property string modelData
                                        readonly property var c: card.ph[modelData]
                                        width: body.width; spacing: 8
                                        RowLayout {
                                            width: 110; height: 40; spacing: 8
                                            FlowToggle { Layout.alignment: Qt.AlignVCenter; checked: !!axis.c
                                                onToggled: (on) => root.edit(d => { if (on) d.phases[card.index][axis.modelData] = root.curve(); else delete d.phases[card.index][axis.modelData] }) }
                                            Text { textFormat: Text.PlainText; Layout.alignment: Qt.AlignVCenter; text: axis.modelData === "vibration" ? qsTr("Vibration") : qsTr("Suction"); color: axis.modelData === "vibration" ? Theme.trainVibration : Theme.trainSuction; font.family: Theme.fontSoft; font.pixelSize: 13 }
                                        }
                                        Repeater {
                                            model: axis.c ? [
                                                ["startLevel", qsTr("Start"), 0, 1, 2], ["peakLevel", qsTr("Peak"), 0, 1, 2], ["endLevel", qsTr("End"), 0, 1, 2],
                                                ["rampUpMs", qsTr("Ramp up ms"), 0, 60000, 0], ["holdMs", qsTr("Hold ms"), 0, 60000, 0],
                                                ["rampDownMs", qsTr("Ramp down ms"), 0, 60000, 0], ["randomJitterFraction", qsTr("Jitter"), 0, 1, 2]] : []
                                            delegate: NumField {
                                                required property var modelData
                                                label: modelData[1]; from: modelData[2]; to: modelData[3]; decimals: modelData[4]
                                                value: axis.c ? axis.c[modelData[0]] : 0
                                                onEdited: (v) => root.edit(d => d.phases[card.index][axis.modelData][modelData[0]] = decimals === 0 ? Math.round(v) : v)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: root.status
                color: root.statusOk ? Theme.good : Theme.orange; font.family: Theme.fontSoft; font.pixelSize: 12
            }
            RowLayout {
                spacing: 8
                FlowButton { text: qsTr("+ Add phase"); onClicked: root.edit(d => d.phases.push(root.phase())) }
                Item { Layout.fillWidth: true }
                FlowButton { text: qsTr("New"); onClicked: root.show("") }
                FlowButton { text: qsTr("Delete"); danger: true; enabled: root.original !== ""
                    onClicked: { const e = root.controller.deleteScript(root.original); if (e) { root.status = e; root.statusOk = false } else root.show("") } }
                FlowButton { text: qsTr("Save script"); primary: true
                    onClicked: {
                        const e = root.controller.saveScript(root.draft)
                        root.statusOk = !e
                        root.status = e ? qsTr("Could not save: %1").arg(e) : qsTr("Saved as \"%1\"").arg(root.draft.name)
                        if (!e) root.original = root.draft.name
                    } }
            }
        }
    }
}
