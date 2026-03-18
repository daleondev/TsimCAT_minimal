import QtQuick
import QtQuick3D

Node {
    id: robotRoot

    // Joint angles in degrees
    property real axis1: 0
    property real axis2: -90
    property real axis3: 90
    property real axis4: 0
    property real axis5: 0
    property real axis6: 0
    property bool gripperGripped: false
    property bool carriedPartVisible: false
    property bool gripperSensorBlocked: false
    onGripperGrippedChanged: console.log("RobotModel: gripperGripped changed to " + gripperGripped)

    eulerRotation.x: -90
    scale: Qt.vector3d(1.6, 1.6, 1.6)

    Node {
        id: robotBase

        Model {
            id: baseLink
            source: "../assets/meshes/base_link/meshes/node3.mesh"
            scale: Qt.vector3d(1000, 1000, 1000)
            materials: [
                PrincipledMaterial {
                    baseColor: "#1a1a1a"
                    metalness: 0.8
                    roughness: 0.2
                }
            ]
        }

        Node {
            id: joint1
            position: Qt.vector3d(0, 0, 450)
            eulerRotation.z: -robotRoot.axis1

            Model {
                id: link1
                source: "../assets/meshes/link_1/meshes/node3.mesh"
                scale: Qt.vector3d(1000, 1000, 1000)
                materials: [
                    PrincipledMaterial {
                        baseColor: "#f67828"
                        metalness: 0.2
                        roughness: 0.4
                    }
                ]
            }

            Node {
                id: joint2
                position: Qt.vector3d(150, 0, 0)
                eulerRotation.y: robotRoot.axis2

                Model {
                    id: link2
                    source: "../assets/meshes/link_2/meshes/node3.mesh"
                    scale: Qt.vector3d(1000, 1000, 1000)
                    materials: [
                        PrincipledMaterial {
                            baseColor: "#f67828"
                            metalness: 0.2
                            roughness: 0.4
                        }
                    ]
                }

                Node {
                    id: joint3
                    position: Qt.vector3d(610, 0, 0)
                    eulerRotation.y: robotRoot.axis3

                    Model {
                        id: link3
                        source: "../assets/meshes/link_3/meshes/node3.mesh"
                        scale: Qt.vector3d(1000, 1000, 1000)
                        materials: [
                            PrincipledMaterial {
                                baseColor: "#f67828"
                                metalness: 0.2
                                roughness: 0.4
                            }
                        ]
                    }

                    Node {
                        id: joint4
                        position: Qt.vector3d(0, 0, 20)
                        eulerRotation.x: -robotRoot.axis4

                        Model {
                            id: link4
                            source: "../assets/meshes/link_4/meshes/node3.mesh"
                            scale: Qt.vector3d(1000, 1000, 1000)
                            materials: [
                                PrincipledMaterial {
                                    baseColor: "#f67828"
                                    metalness: 0.2
                                    roughness: 0.4
                                }
                            ]
                        }

                        Node {
                            id: joint5
                            position: Qt.vector3d(660, 0, 0)
                            eulerRotation.y: robotRoot.axis5

                            Model {
                                id: link5
                                source: "../assets/meshes/link_5/meshes/node3.mesh"
                                scale: Qt.vector3d(1000, 1000, 1000)
                                materials: [
                                    PrincipledMaterial {
                                        baseColor: "#f67828"
                                        metalness: 0.2
                                        roughness: 0.4
                                    }
                                ]
                            }

                            Node {
                                id: joint6
                                position: Qt.vector3d(80, 0, 0)
                                eulerRotation.x: -robotRoot.axis6

                                Model {
                                    id: link6
                                    source: "../assets/meshes/link_6/meshes/node3.mesh"
                                    scale: Qt.vector3d(1000, 1000, 1000)
                                    materials: [
                                        PrincipledMaterial {
                                            baseColor: "#2a2a2a"
                                            metalness: 0.9
                                            roughness: 0.1
                                        }
                                    ]
                                }

                                Node {
                                    id: flange

                                    // Mounting Plate (Physical part of the robot)
                                    Model {
                                        source: "#Cylinder"
                                        scale: Qt.vector3d(0.8, 0.1, 0.8)
                                        eulerRotation.z: 90
                                        materials: [
                                            PrincipledMaterial {
                                                baseColor: "#1a1a1a"
                                                metalness: 0.9
                                            }
                                        ]
                                    }

                                    Node {
                                        id: tool0
                                        position: Qt.vector3d(15, 0, 0) // Offset along X (Forward for Axis 6)

                                        GripperModel {
                                            id: robotGripper
                                            eulerRotation.y: 90
                                            eulerRotation.z: 90
                                            gripped: robotRoot.gripperGripped
                                            sensorBlocked: robotRoot.gripperSensorBlocked
                                        }

                                        PartModel {
                                            visible: robotRoot.carriedPartVisible
                                            position: Qt.vector3d(100, 0, -25)
                                            eulerRotation.y: 90
                                            eulerRotation.z: 90
                                            scale: Qt.vector3d(0.625, 0.625, 0.625)
                                            width: 140
                                            length: 140
                                            height: 80
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
