import QtQuick
import QtQuick3D

Node {
    id: partRoot
    property real width: 100
    property real length: 100
    property real height: 50
    property color color: "#a6adb3"

    Model {
        source: "#Cube"
        scale: Qt.vector3d(partRoot.length / 100, partRoot.height / 100, partRoot.width / 100)
        position: Qt.vector3d(0, partRoot.height / 2, 0)
        materials: [
            PrincipledMaterial {
                baseColor: partRoot.color
                metalness: 0.35
                roughness: 0.42
            }
        ]
    }
}
