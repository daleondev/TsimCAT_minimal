import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../controls" as Controls

Control {
    id: root
    required property var backend
    
    contentItem: Item {
        Controls.Plant3DView {
            id: plantView
            anchors.fill: parent
            backend: root.backend
            exitDamperOpen: root.backend && root.backend.exitConveyor ? root.backend.exitConveyor.damperOpen : false
        }
    }
}
