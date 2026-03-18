import QtQuick
import QtQuick.Controls
import QtQuick3D

Item {
    id: root
    property var backend: null
    property bool exitDamperOpen: false

    View3D {
        id: view
        anchors.fill: parent
        camera: sceneCamera

        environment: SceneEnvironment {
            clearColor: "#dbe3e0"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }

        Node {
            id: cameraPivot
            eulerRotation: Qt.vector3d(-26, 34, 0)
            position: Qt.vector3d(520, 420, 120)

            PerspectiveCamera {
                id: sceneCamera
                position: Qt.vector3d(0, 0, 6100)
                clipNear: 10
                clipFar: 30000
            }
        }

        DirectionalLight {
            eulerRotation.x: -38
            eulerRotation.y: -18
            brightness: 1.55
            castsShadow: true
            shadowMapFar: 12000
            shadowMapQuality: DirectionalLight.ShadowMapQualityVeryHigh
        }

        PointLight {
            position: Qt.vector3d(1200, 2400, 1400)
            brightness: 0.9
            color: "#f2f1df"
        }

        PointLight {
            position: Qt.vector3d(-2200, 1800, -800)
            brightness: 0.45
            color: "#bfd0df"
        }

        Model {
            y: -1
            source: "#Rectangle"
            scale: Qt.vector3d(180, 180, 1)
            eulerRotation.x: -90
            materials: [
                PrincipledMaterial {
                    baseColor: "#c6ccc6"
                    roughness: 0.98
                }
            ]
        }

        Model {
            y: 1
            source: "#Rectangle"
            position: Qt.vector3d(80, 0, 120)
            scale: Qt.vector3d(36, 34, 1)
            eulerRotation.x: -90
            materials: [
                PrincipledMaterial {
                    baseColor: "#b7b49f"
                    roughness: 0.95
                }
            ]
        }

        FenceModel {
            id: fence
            position: Qt.vector3d(150, 0, 120)
            exitDamperOpen: root.exitDamperOpen
        }

        Model {
            position: Qt.vector3d(-1420, 4, 120)
            source: "#Rectangle"
            scale: Qt.vector3d(9, 12, 1)
            eulerRotation.x: -90
            materials: [
                PrincipledMaterial {
                    baseColor: "#83918d"
                    roughness: 0.9
                }
            ]
        }

        Node {
            id: table
            position: Qt.vector3d(-1350, 0, 120)

            RotaryTableModel {
                angleDegrees: root.backend ? root.backend.rotaryTable.angleDegrees : 0
                part0Present: root.backend ? root.backend.rotaryTable.part0Present : false
                part180Present: root.backend ? root.backend.rotaryTable.part180Present : false
                sensor0Active: root.backend ? root.backend.rotaryTable.sensor0Active : false
                sensor180Active: root.backend ? root.backend.rotaryTable.sensor180Active : false
            }
        }

        ConveyorModel {
            id: conveyor
            position: Qt.vector3d(1640, 0, 120)
            length: 1250
            height: 720
            width: 430
            running: root.backend ? root.backend.conveyor.running : false
            sensors: root.backend ? root.backend.conveyor.sensors : [false, false, false, false]
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

        property real lastX: 0
        property real lastY: 0

        onPressed: mouse => {
            lastX = mouse.x;
            lastY = mouse.y;
        }

        onPositionChanged: mouse => {
            let dx = mouse.x - lastX;
            let dy = mouse.y - lastY;
            if (mouse.buttons & Qt.RightButton) {
                cameraPivot.eulerRotation.y -= dx * 0.2;
                cameraPivot.eulerRotation.x = Math.max(-90, Math.min(0, cameraPivot.eulerRotation.x - dy * 0.2));
            } else if (mouse.buttons & (Qt.LeftButton | Qt.MiddleButton)) {
                let speed = sceneCamera.position.z / 2000.0;
                let right = sceneCamera.mapDirectionToScene(Qt.vector3d(1, 0, 0));
                let up = sceneCamera.mapDirectionToScene(Qt.vector3d(0, 1, 0));
                let move = right.times(-dx * speed).plus(up.times(dy * speed));
                cameraPivot.position = cameraPivot.position.plus(move);
            }
            lastX = mouse.x;
            lastY = mouse.y;
        }

        onWheel: wheel => {
            let zoomSpeed = sceneCamera.position.z * 0.1;
            sceneCamera.position.z = Math.max(500, Math.min(15000, sceneCamera.position.z - (wheel.angleDelta.y / 120.0) * zoomSpeed));
        }
    }
}
