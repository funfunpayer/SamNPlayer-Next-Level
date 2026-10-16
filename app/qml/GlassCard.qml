import QtQuick
import SamN

// A glass panel of the Flow look: dark blue gradient, thin blue border.
Rectangle {
    radius: Theme.radius
    border.color: Theme.cardBorder
    border.width: 1
    gradient: Gradient {
        GradientStop { position: 0; color: Theme.cardTop }
        GradientStop { position: 1; color: Theme.cardBottom }
    }
}
