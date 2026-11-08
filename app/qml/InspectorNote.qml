import QtQuick
import SamN

Text {
    textFormat: Text.PlainText
    width: parent ? parent.width : 260
    wrapMode: Text.Wrap
    color: Theme.textMuted; font.family: Theme.fontSoft; font.pixelSize: 12
}
// One output channel: on / off and a level (0.7 SetChannelLevels).
