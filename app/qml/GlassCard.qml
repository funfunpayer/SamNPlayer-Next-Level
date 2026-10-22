import QtQuick
import SamN

// A glass panel of the Flow look: dark blue gradient, thin blue border,
// soft top sheen so cards feel lit rather than flat.
Rectangle {
    id: root
    radius: Theme.radius
    border.color: Theme.cardBorder
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0; color: Theme.cardTop }
        GradientStop { position: 1; color: Theme.cardBottom }
    }
    // Hairline highlight along the top edge (glass catch-light).
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 1
        height: 1
        radius: 1
        color: Qt.rgba(180 / 255, 220 / 255, 255 / 255, 0.10)
    }
}
