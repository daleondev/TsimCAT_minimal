import QtQuick
import QtQuick3D

Node {
    id: root
    property bool gripped: false
    property bool sensorBlocked: false
    property real gripperLength: 200
    property real gripperWidth: 100

    Model {
        source: "#Cube"
        scale: Qt.vector3d(root.gripperLength / 100, root.gripperWidth / 100, root.gripperWidth / 100)
        materials: [
            PrincipledMaterial {
                baseColor: '#6e6e6e'
                metalness: 0.6
                roughness: 0.4
            }
        ]
    }

    Node {
        id: leftFinger
        x: root.gripped ? 50 : 75
        z: root.gripperWidth - 10

        Behavior on x {
            NumberAnimation {
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }

        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.1, 0.4, 0.8)
            materials: [
                PrincipledMaterial {
                    baseColor: "#b1b1b1"
                    metalness: 0.6
                }
            ]
        }
    }

    Node {
        id: rightFinger
        x: root.gripped ? -50 : -75
        z: root.gripperWidth - 10

        Behavior on x {
            NumberAnimation {
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }

        Model {
            source: "#Cube"
            scale: Qt.vector3d(0.1, 0.4, 0.8)
            materials: [
                PrincipledMaterial {
                    baseColor: '#b1b1b1'
                    metalness: 0.6
                }
            ]
        }
    }

    // Gripper light barrier sensor
    Model {
        source: "#Sphere"
        position: Qt.vector3d(0, root.gripperLength / 100, root.gripperWidth + 15)
        scale: Qt.vector3d(0.12, 0.12, 0.12)
        materials: [
            DefaultMaterial {
                diffuseColor: root.sensorBlocked ? "#e74c3c" : "#2ecc71"
                emissiveFactor: Qt.vector3d(diffuseColor.r, diffuseColor.g, diffuseColor.b)
            }
        ]
    }
}
