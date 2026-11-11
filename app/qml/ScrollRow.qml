import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

// A row of controls that scrolls sideways when it is wider than its place,
// so a narrow window cuts nothing off for good. Children take the usual
// Layout.* properties of a RowLayout.
Flickable {
    id: root
    default property alias content: row.data
    property alias spacing: row.spacing
    readonly property bool overflows: row.implicitWidth > width + 0.5
    // The bar overlays the row's lower edge: the row's height never depends on
    // whether it overflows, so the video above does not jump.
    implicitHeight: row.implicitHeight
    contentWidth: row.width; contentHeight: row.implicitHeight
    clip: true
    interactive: overflows
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.HorizontalFlick
    C.ScrollBar.horizontal: C.ScrollBar {
        objectName: "scrollRowBar"
        height: 6
        policy: root.overflows ? C.ScrollBar.AlwaysOn : C.ScrollBar.AlwaysOff
    }
    // The wheel moves the row sideways while it overflows.
    WheelHandler {
        enabled: root.overflows
        onWheel: (event) => {
            const d = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
            root.contentX = Math.max(0, Math.min(root.contentWidth - root.width, root.contentX - d))
        }
    }
    RowLayout {
        id: row
        width: Math.max(root.width, implicitWidth)
    }
}
