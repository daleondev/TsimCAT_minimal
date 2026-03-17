pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Node {
    id: rotaryRoot

    property real radius: 480
    property real height: 760
    property real angleDegrees: 0
    property bool sensor0Active: false
    property bool sensor180Active: false

    readonly property real platterScale: rotaryRoot.radius / 50
    readonly property real innerPlatterScale: rotaryRoot.platterScale * 0.8
    readonly property real pedestalScale: rotaryRoot.radius / 95


    PrincipledMaterial {
        id: accentPaint
        baseColor: "#3f6d7a"
        metalness: 0.28
        roughness: 0.36
    }

    PrincipledMaterial {
        id: machineBasePaint
        baseColor: "#2a3138"
        metalness: 0.35
        roughness: 0.46
    }

    PrincipledMaterial {
        id: trimPaint
        baseColor: "#6e7b83"
        metalness: 0.42
        roughness: 0.32
    }

    Model {
        source: "#Cube"
        position: Qt.vector3d(0, 90, 0)
        scale: Qt.vector3d(rotaryRoot.platterScale * 1, 1.8, rotaryRoot.platterScale * 1)
        materials: [ machineBasePaint ]
    }

    Model {
        source: "#Cube"
        position: Qt.vector3d(0, 245, 0)
        scale: Qt.vector3d(rotaryRoot.platterScale * 0.75, 1.35, rotaryRoot.platterScale * 0.75)
        materials: [ trimPaint ]
    }

    Model {
        source: "#Cylinder"
        position: Qt.vector3d(0, rotaryRoot.height - 190, 0)
        scale: Qt.vector3d(rotaryRoot.pedestalScale * 0.76, 5.3, rotaryRoot.pedestalScale * 0.76)
        materials: [ machineBasePaint ]
    }

    Node {
        id: platter
        position: Qt.vector3d(0, rotaryRoot.height, 0)
        eulerRotation.y: rotaryRoot.angleDegrees

        Model {
            source: "#Cylinder"
            scale: Qt.vector3d(rotaryRoot.platterScale, 0.42, rotaryRoot.platterScale)
            materials: [ accentPaint ]
        }

        Model {
            source: "#Cylinder"
            position: Qt.vector3d(0, 18, 0)
            scale: Qt.vector3d(rotaryRoot.innerPlatterScale, 0.18, rotaryRoot.innerPlatterScale)
            materials: [ machineBasePaint ]
        }

        // Center safety shield
        Model {
            source: "#Cylinder"
            position: Qt.vector3d(0, 170, 0)
            scale: Qt.vector3d(1.2, 3.0, 1.2)
            materials: [ machineBasePaint ]
        }

        Model {
            source: "#Cylinder"
            position: Qt.vector3d(0, 320, 0)
            scale: Qt.vector3d(2.4, 0.3, 2.4)
            materials: [ machineBasePaint ]
        }

        Model {
            source: "#Cube"
            position: Qt.vector3d(0, 195, 0)
            scale: Qt.vector3d(0.2, 2.2, rotaryRoot.platterScale * 1)
            materials: [ machineBasePaint ]
        }

        // PartModel { // PART
        //     visible: rotaryRoot.partPresent
        //     position: Qt.vector3d(rotaryRoot.radius * 0.5, 58, 0)
        //     width: 140
        //     length: 140
        //     height: 80
        //     color: "#a6adb3"
        // }
    }

    // Sensor indicators (like conveyor light barriers)
    // Part presence sensor — at the load side of the platter
    Model {
        source: "#Sphere"
        position: Qt.vector3d(-rotaryRoot.radius * 0.9, rotaryRoot.height + 40, 0)
        scale: Qt.vector3d(0.2, 0.2, 0.2)
        materials: [
            DefaultMaterial {
                diffuseColor: rotaryRoot.sensor0Active ? "#e74c3c" : "#2ecc71"
                emissiveFactor: Qt.vector3d(diffuseColor.r, diffuseColor.g, diffuseColor.b)
            }
        ]
    }

    // Ready-to-pick sensor
    Model {
        source: "#Sphere"
        position: Qt.vector3d(rotaryRoot.radius * 0.9, rotaryRoot.height + 40, 0)
        scale: Qt.vector3d(0.2, 0.2, 0.2)
        materials: [
            DefaultMaterial {
                diffuseColor: rotaryRoot.sensor180Active ? "#e74c3c" : "#2ecc71"
                emissiveFactor: Qt.vector3d(diffuseColor.r, diffuseColor.g, diffuseColor.b)
            }
        ]
    }
}