import QtQuick
import QtQuick.Window
import SamN

// The loading screen (Owner 26 Oct): shown at once while the interface is
// built, so the main window appears whole instead of piece by piece. The
// bar moves on the render thread (an Animator), so it keeps moving while
// the main window is being created.
Window {
    id: splash
    objectName: "splash"
    width: 460; height: 280
    flags: Qt.SplashScreen | Qt.FramelessWindowHint
    color: "transparent"
    title: "SamNPlayer Next"
    visible: true

    Rectangle {
        anchors.fill: parent
        radius: 18
        border.color: Qt.rgba(1, 1, 1, 0.08)
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.bgTop }
            GradientStop { position: 1; color: Theme.bgBottom }
        }
        Column {
            anchors.centerIn: parent
            spacing: 16
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "assets/logo.png"
                width: 72; height: 72; fillMode: Image.PreserveAspectFit
                sourceSize.width: 144
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: "SamNPlayer Next"
                color: Theme.text; font.family: Theme.font; font.pixelSize: 22; font.weight: Font.DemiBold
            }
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 220; height: 3
                clip: true
                Rectangle { anchors.fill: parent; radius: 1.5; color: Qt.rgba(1, 1, 1, 0.08) }
                Rectangle {
                    id: bar
                    width: 70; height: parent.height; radius: 1.5
                    x: Theme.reducedMotion ? (parent.width - width) / 2 : -width
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.blue }
                        GradientStop { position: 1; color: Theme.cyan }
                    }
                    XAnimator on x {
                        running: !Theme.reducedMotion
                        loops: Animation.Infinite
                        from: -bar.width; to: 220; duration: 1300
                        easing.type: Easing.InOutSine
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                textFormat: Text.PlainText
                text: qsTr("Loading…")
                color: Theme.textDim; font.family: Theme.fontSoft; font.pixelSize: 13
            }
        }
    }
}
