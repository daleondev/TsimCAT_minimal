pragma ComponentBehavior: Bound
import QtQuick
import QtQuick3D

Node {
    id: fenceRoot
    readonly property real width: 3000
    readonly property real depth: 3000
    readonly property real height: 2000
    readonly property real fencePanelWidth: 1000

    property bool exitDamperOpen: false

    // Common Material for frame/posts
    PrincipledMaterial {
        id: fenceMaterial
        baseColor: '#5e5e5e'
        metalness: 0.8
    }

    // Common Material for frame/posts
    PrincipledMaterial {
        id: postMaterial
        baseColor: "#222222"
        metalness: 0.8
    }

    component WireMesh: Node {
        id: wireMesh
        property real w: fenceRoot.fencePanelWidth
        property real h: fenceRoot.height
        property bool isSolid: false
        property real wireScale: isSolid ? 0.06 : 0.02

        // Vertical Wires
        Repeater3D {
            visible: !wireMesh.isSolid
            model: Math.ceil(wireMesh.w / 100) + 1
            delegate: Model {
                required property int index
                position: Qt.vector3d(-wireMesh.w / 2 + index * 100, wireMesh.h / 2, 0)
                source: "#Cube"
                scale: Qt.vector3d(wireMesh.wireScale, wireMesh.h / 100, wireMesh.wireScale)
                materials: [fenceMaterial]
            }
        }

        // Horizontal Wires
        Repeater3D {
            visible: !wireMesh.isSolid
            model: Math.ceil(wireMesh.h / 100) + 1
            delegate: Model {
                required property int index
                position: Qt.vector3d(0, index * 100, 0)
                source: "#Cube"
                scale: Qt.vector3d(wireMesh.w / 100, wireMesh.wireScale, wireMesh.wireScale)
                materials: [fenceMaterial]
            }
        }

        Model {
            id: backing
            visible: wireMesh.isSolid
            position: Qt.vector3d(0, wireMesh.h / 2, 0)
            source: "#Cube"
            scale: Qt.vector3d(wireMesh.w / 100, wireMesh.h / 100, 0.01)
            materials: [
                PrincipledMaterial {
                    baseColor: '#5e5e5e'
                    // opacity: 0.32
                    opacity: 1
                    alphaMode: PrincipledMaterial.Blend
                    cullMode: PrincipledMaterial.NoCulling
                }
            ]
        }
    }

    // Post component
    component Post: Model {
        property vector3d externalScale: Qt.vector3d(1, 1, 1)
        readonly property vector3d internalScale: Qt.vector3d(0.5, fenceRoot.height / 100, 0.5)
        source: "#Cube"
        scale: internalScale.times(externalScale)
        materials: [postMaterial]
    }

    // Bar component
    component Bar: Model {
        property vector3d externalScale: Qt.vector3d(1, 1, 1)
        readonly property vector3d internalScale: Qt.vector3d(fenceRoot.fencePanelWidth / 100, 0.3, 0.3)
        source: "#Cube"
        scale: internalScale.times(externalScale)
        materials: [postMaterial]
    }

    // Helper for a single fence panel segment
    component FencePanel: Node {
        id: fencePanel
        property real panelWidth: fenceRoot.fencePanelWidth
        property bool showMesh: true
        property bool showLeftPost: true
        property alias isSolid: wire.isSolid

        Bar {
            position: Qt.vector3d(0, 0, 0)
        }

        Post {
            position: Qt.vector3d(fencePanel.panelWidth / 2, 1000, 0)
        }

        WireMesh {
            id: wire
            visible: fencePanel.showMesh
            w: fencePanel.panelWidth
            h: 2000
        }

        Post {
            position: Qt.vector3d(-fencePanel.panelWidth / 2, 1000, 0)
        }

        Bar {
            position: Qt.vector3d(0, fenceRoot.height, 0)
        }
    }

    // Guillotine Damper Component
    component GuillotineDamper: Node {
        id: guillotineDamper
        property real ratio: 0.6
        property bool open: false

        // Moving Blade
        Node {
            id: damperBlade
            y: (1 - guillotineDamper.ratio) * fenceRoot.height + (guillotineDamper.open ? 600 : 0)

            Behavior on y {
                NumberAnimation {
                    duration: 1500
                    easing.type: Easing.InOutQuad
                }
            }

            FencePanel {
                scale: Qt.vector3d(1, guillotineDamper.ratio, 1)
                isSolid: true
            }
        }
    }

    // --- ASSEMBLE PERIMETER ---

    // Back Side
    Node {
        position: Qt.vector3d(0, 0, -fenceRoot.depth / 2)

        Repeater3D {
            model: 3
            delegate: FencePanel {
                required property int index
                position: Qt.vector3d(-1000 + index * 1000, 0, 0)
            }
        }
    }

    // Front Side
    Node {
        position: Qt.vector3d(0, 0, fenceRoot.depth / 2)

        Repeater3D {
            model: 3
            delegate: FencePanel {
                required property int index
                position: Qt.vector3d(-1000 + index * 1000, 0, 0)
            }
        }
    }

    // Left Side (Entry opening for rotary table)
    Node {
        position: Qt.vector3d(-fenceRoot.width / 2, 0, 0)
        eulerRotation.y: 90

        FencePanel {
            position: Qt.vector3d(-1000, 0, 0)
        }

        Post {
            position: Qt.vector3d(-500, 1000, 0)
        }

        Post {
            position: Qt.vector3d(500, 1000, 0)
        }

        Bar {
            position: Qt.vector3d(0, fenceRoot.height, 0)
        }

        // Safety barrier above rotary table entry gap
        FencePanel {
            position: Qt.vector3d(0, fenceRoot.height * 0.55, 0)
            panelWidth: 1000
            scale: Qt.vector3d(1, 0.45, 1)
        }

        FencePanel {
            position: Qt.vector3d(1000, 0, 0)
        }
    }

    // Right Side (Open for transfer)
    Node {
        position: Qt.vector3d(fenceRoot.width / 2, 0, 0)
        eulerRotation.y: 90

        FencePanel {
            position: Qt.vector3d(-1000, 0, 0)
        }

        FencePanel {
            scale: Qt.vector3d(1, 0.3, 1)
        }

        GuillotineDamper {
            position: Qt.vector3d(0, 0, 0)
            open: fenceRoot.exitDamperOpen
        }

        // Middle area is open
        FencePanel {
            position: Qt.vector3d(1000, 0, 0)
        }
    }
}
