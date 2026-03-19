pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Node {
    id: conveyorRoot
    property real length: 2000
    property real width: 400
    property real height: 800
    property bool partPresent: false
    property real partPosition: 0
    property var partPositions: []
    property bool running: false
    property var sensors: [false, false, false, false]

    readonly property var sensorPositions: [100, 500, 750, 1150]

    readonly property real frameThickness: 50

    // Frame
    Model {
        position: Qt.vector3d(0, conveyorRoot.height - conveyorRoot.frameThickness / 2, 0)
        source: "#Cube"
        scale: Qt.vector3d(conveyorRoot.length / 100, conveyorRoot.frameThickness / 100, conveyorRoot.width / 100)
        materials: [
            PrincipledMaterial {
                baseColor: "#3f6d7a"
                metalness: 0.28
                roughness: 0.36
            }
        ]
    }

    // Belt surface
    Model {
        position: Qt.vector3d(0, conveyorRoot.height + 5, 0) // On top of frame
        source: "#Cube"
        scale: Qt.vector3d(conveyorRoot.length / 100 - 0.1, 0.1, conveyorRoot.width / 100 - 0.2)
        materials: [
            PrincipledMaterial {
                baseColor: conveyorRoot.running ? "#2d2d2d" : "#1a1a1a"
                emissiveFactor: conveyorRoot.running ? Qt.vector3d(0.08, 0.08, 0.08) : Qt.vector3d(0, 0, 0)
                roughness: 0.9
                metalness: 0.0
            }
        ]
    }

    // Legs
    Repeater3D {
        model: 4
        delegate: Model {
            required property int index
            // Positioned so the top of the leg is at the bottom of the frame
            position: Qt.vector3d((index % 2 === 0 ? -1 : 1) * (conveyorRoot.length / 2 - 100), (conveyorRoot.height - conveyorRoot.frameThickness) / 2, (index < 2 ? -1 : 1) * (conveyorRoot.width / 2 - 50))
            source: "#Cube"
            scale: Qt.vector3d(0.5, (conveyorRoot.height - conveyorRoot.frameThickness) / 100, 0.5)
            materials: [
                PrincipledMaterial {
                    baseColor: "#7f8c8d"
                    metalness: 0.8
                }
            ]
        }
    }

    // Sensors (Light Barriers)
    Repeater3D {
        model: conveyorRoot.sensorPositions.length
        delegate: Node {
            id: sensorNode
            required property int index
            // Position relative to conveyor center
            position: Qt.vector3d(conveyorRoot.sensorPositions[sensorNode.index] - conveyorRoot.length / 2, conveyorRoot.height + 60, conveyorRoot.width / 2 - 20)

            Model {
                source: "#Sphere"
                scale: Qt.vector3d(0.2, 0.2, 0.2)
                materials: [
                    DefaultMaterial {
                        readonly property color activeColor: "#e74c3c"
                        readonly property color inactiveColor: "#3a4348"
                        diffuseColor: conveyorRoot.sensors[sensorNode.index] ? activeColor : inactiveColor
                        emissiveFactor: conveyorRoot.sensors[sensorNode.index] ? Qt.vector3d(activeColor.r, activeColor.g, activeColor.b) : Qt.vector3d(0, 0, 0)
                    }
                ]
            }
        }
    }

    Repeater3D {
        model: conveyorRoot.partPositions.length
        delegate: PartModel {
            required property int index
            position: Qt.vector3d(conveyorRoot.partPositions[index] - conveyorRoot.length / 2, conveyorRoot.height + 10, 0)
            width: 140
            length: 140
            height: 80
            color: "#a6adb3"
        }
    }
}
