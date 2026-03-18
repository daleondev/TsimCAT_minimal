import QtQuick
import QtQuick.Controls
import QtQuick3D

Item {
    id: root
    property var backend: null
    property bool exitDamperOpen: false
    property bool robotPanelVisible: false
    property real jointJogStepDegrees: 5
    property real cartesianJogStepMillimeters: 25
    property real orientationJogStepDegrees: 5

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

        RobotModel {
            id: robot
            position: Qt.vector3d(0, 0, 1000)
            eulerRotation.z: 90

            axis1: root.backend ? root.backend.robot.axis1 : 0
            axis2: root.backend ? root.backend.robot.axis2 : -90
            axis3: root.backend ? root.backend.robot.axis3 : 90
            axis4: root.backend ? root.backend.robot.axis4 : 0
            axis5: root.backend ? root.backend.robot.axis5 : 0
            axis6: root.backend ? root.backend.robot.axis6 : 0
            gripperGripped: root.backend ? root.backend.robot.gripperGripped : false
            carriedPartVisible: root.backend ? root.backend.robot.carriedPartVisible : false
            gripperSensorBlocked: root.backend ? root.backend.robot.gripperSensorBlocked : false
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

    Rectangle {
        id: robotPanel
        z: 2
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.rightMargin: 16
        width: 320
        height: parent.height - 32
        radius: 18
        visible: root.robotPanelVisible
        color: "#f3efe5"
        border.color: "#6d7b75"
        border.width: 1
        opacity: 0.96

        ScrollView {
            anchors.fill: parent
            anchors.margins: 14
            clip: true

            Column {
                width: robotPanel.width - 28
                spacing: 14

                Label {
                    width: parent.width
                    text: "Robot Control"
                    font.pixelSize: 24
                    font.bold: true
                    color: "#26322f"
                }

                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.backend && root.backend.robot ? "Job " + root.backend.robot.activeJobId + "  |  Motion " + (root.backend.robot.inMotion ? "running" : "idle") + "  |  Mode " + (root.backend.robot.manualMode ? "manual" : "auto") : "Robot backend unavailable"
                    color: "#43524d"
                }

                Row {
                    spacing: 10

                    Button {
                        text: root.backend && root.backend.robot && root.backend.robot.manualMode ? "Exit Manual" : "Manual"
                        enabled: root.backend && root.backend.robot
                        onClicked: root.backend.robot.setManualMode(!root.backend.robot.manualMode)
                    }

                    Button {
                        text: "Home"
                        enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                        onClicked: root.backend.robot.executeJob(1)
                    }
                }

                Column {
                    width: parent.width
                    spacing: 8

                    Label {
                        text: "Jobs"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#26322f"
                    }

                    Row {
                        spacing: 8

                        Button {
                            text: "Pick Table"
                            enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                            onClicked: root.backend.robot.executeJob(2)
                        }

                        Button {
                            text: "Place Conveyor"
                            enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                            onClicked: root.backend.robot.executeJob(5)
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 8

                    Label {
                        text: "Joint Jog"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#26322f"
                    }

                    SpinBox {
                        id: jointStepBox
                        from: 1
                        to: 45
                        value: root.jointJogStepDegrees
                        editable: true
                        textFromValue: function (value, locale) {
                            return Number(value).toLocaleString(locale, 'f', 0) + " deg";
                        }
                        valueFromText: function (text, locale) {
                            return Number.fromLocaleString(locale, text.replace(" deg", ""));
                        }
                        onValueModified: root.jointJogStepDegrees = value
                    }

                    Repeater {
                        model: [
                            {
                                label: "A1",
                                angle: () => root.backend.robot.axis1,
                                axis: 0
                            },
                            {
                                label: "A2",
                                angle: () => root.backend.robot.axis2,
                                axis: 1
                            },
                            {
                                label: "A3",
                                angle: () => root.backend.robot.axis3,
                                axis: 2
                            },
                            {
                                label: "A4",
                                angle: () => root.backend.robot.axis4,
                                axis: 3
                            },
                            {
                                label: "A5",
                                angle: () => root.backend.robot.axis5,
                                axis: 4
                            },
                            {
                                label: "A6",
                                angle: () => root.backend.robot.axis6,
                                axis: 5
                            }
                        ]

                        delegate: Row {
                            required property var modelData
                            width: parent.width
                            spacing: 8

                            Label {
                                width: 34
                                text: modelData.label
                                color: "#26322f"
                                verticalAlignment: Text.AlignVCenter
                            }

                            Button {
                                text: "-"
                                enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                                onClicked: root.backend.robot.jogJoint(modelData.axis, -root.jointJogStepDegrees)
                            }

                            Label {
                                width: 96
                                text: root.backend && root.backend.robot ? Number(modelData.angle()).toFixed(1) + " deg" : "-"
                                color: "#43524d"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            Button {
                                text: "+"
                                enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                                onClicked: root.backend.robot.jogJoint(modelData.axis, root.jointJogStepDegrees)
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 8

                    Label {
                        text: "Cartesian Jog"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#26322f"
                    }

                    Row {
                        spacing: 8

                        SpinBox {
                            id: cartesianStepBox
                            from: 5
                            to: 200
                            value: root.cartesianJogStepMillimeters
                            editable: true
                            textFromValue: function (value, locale) {
                                return Number(value).toLocaleString(locale, 'f', 0) + " mm";
                            }
                            valueFromText: function (text, locale) {
                                return Number.fromLocaleString(locale, text.replace(" mm", ""));
                            }
                            onValueModified: root.cartesianJogStepMillimeters = value
                        }

                        SpinBox {
                            id: orientationStepBox
                            from: 1
                            to: 45
                            value: root.orientationJogStepDegrees
                            editable: true
                            textFromValue: function (value, locale) {
                                return Number(value).toLocaleString(locale, 'f', 0) + " deg";
                            }
                            valueFromText: function (text, locale) {
                                return Number.fromLocaleString(locale, text.replace(" deg", ""));
                            }
                            onValueModified: root.orientationJogStepDegrees = value
                        }
                    }

                    Grid {
                        columns: 3
                        rowSpacing: 8
                        columnSpacing: 8

                        Repeater {
                            model: [
                                {
                                    label: "X-",
                                    dx: -1,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "X+",
                                    dx: 1,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "Y-",
                                    dx: 0,
                                    dy: -1,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "Y+",
                                    dx: 0,
                                    dy: 1,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "Z-",
                                    dx: 0,
                                    dy: 0,
                                    dz: -1,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "Z+",
                                    dx: 0,
                                    dy: 0,
                                    dz: 1,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "R-",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: -1,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "R+",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: 1,
                                    dp: 0,
                                    dyaw: 0
                                },
                                {
                                    label: "P-",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: -1,
                                    dyaw: 0
                                },
                                {
                                    label: "P+",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: 1,
                                    dyaw: 0
                                },
                                {
                                    label: "Yaw-",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: -1
                                },
                                {
                                    label: "Yaw+",
                                    dx: 0,
                                    dy: 0,
                                    dz: 0,
                                    dr: 0,
                                    dp: 0,
                                    dyaw: 1
                                }
                            ]

                            delegate: Button {
                                required property var modelData
                                text: modelData.label
                                enabled: root.backend && root.backend.robot && !root.backend.robot.inMotion
                                onClicked: root.backend.robot.jogCartesian(modelData.dx * root.cartesianJogStepMillimeters, modelData.dy * root.cartesianJogStepMillimeters, modelData.dz * root.cartesianJogStepMillimeters, modelData.dr * root.orientationJogStepDegrees, modelData.dp * root.orientationJogStepDegrees, modelData.dyaw * root.orientationJogStepDegrees)
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 6

                    Label {
                        text: "Tool Pose"
                        font.pixelSize: 18
                        font.bold: true
                        color: "#26322f"
                    }

                    Label {
                        text: root.backend && root.backend.robot ? "X " + Number(root.backend.robot.toolX).toFixed(1) + " | Y " + Number(root.backend.robot.toolY).toFixed(1) + " | Z " + Number(root.backend.robot.toolZ).toFixed(1) : "-"
                        color: "#43524d"
                    }

                    Label {
                        text: root.backend && root.backend.robot ? "R " + Number(root.backend.robot.toolRollDegrees).toFixed(1) + " | P " + Number(root.backend.robot.toolPitchDegrees).toFixed(1) + " | Y " + Number(root.backend.robot.toolYawDegrees).toFixed(1) : "-"
                        color: "#43524d"
                    }
                }
            }
        }
    }

    Button {
        id: robotPanelToggle
        z: 2
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 16
        anchors.rightMargin: 16
        visible: !root.robotPanelVisible
        text: "Robot"
        onClicked: root.robotPanelVisible = true
    }

    Button {
        z: 2
        anchors.top: parent.top
        anchors.right: robotPanel.right
        anchors.topMargin: 24
        anchors.rightMargin: 24
        visible: root.robotPanelVisible
        text: "Close"
        onClicked: root.robotPanelVisible = false
    }

    MouseArea {
        z: 1
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

        property real lastX: 0
        property real lastY: 0

        onPressed: mouse => {
            mouse.accepted = !robotPanel.visible || mouse.x < robotPanel.x;
            if (!mouse.accepted) {
                return;
            }
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
