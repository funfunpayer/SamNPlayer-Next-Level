import QtQuick
import SamN

// A mode that is not built yet says so plainly (no pretend features).
Item {
    property string mode
    property string phase
    Text {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 560)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.family: Theme.fontSoft
        font.pixelSize: 16
        text: qsTr("%1 is not built yet in SamNPlayer 2 (phase %2, docs/PLAN.md).\nUse SamNPlayer 0.7 for it until then.").arg(mode).arg(phase)
    }
}
