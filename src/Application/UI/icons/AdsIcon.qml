import QtQuick
import QtQuick.Shapes

Shape {
    id: iconRoot
    property color color: "white"
    width: 24
    height: 24
    anchors.centerIn: parent
    antialiasing: true

    // Network hub icon: central square with four connection lines
    ShapePath {
        fillColor: iconRoot.color
        strokeColor: "transparent"

        // Central square
        PathMove { x: 9; y: 9 }
        PathLine { x: 15; y: 9 }
        PathLine { x: 15; y: 15 }
        PathLine { x: 9; y: 15 }
        PathLine { x: 9; y: 9 }

        // Top arm
        PathMove { x: 11; y: 3 }
        PathLine { x: 13; y: 3 }
        PathLine { x: 13; y: 9 }
        PathLine { x: 11; y: 9 }
        PathLine { x: 11; y: 3 }

        // Bottom arm
        PathMove { x: 11; y: 15 }
        PathLine { x: 13; y: 15 }
        PathLine { x: 13; y: 21 }
        PathLine { x: 11; y: 21 }
        PathLine { x: 11; y: 15 }

        // Left arm
        PathMove { x: 3; y: 11 }
        PathLine { x: 9; y: 11 }
        PathLine { x: 9; y: 13 }
        PathLine { x: 3; y: 13 }
        PathLine { x: 3; y: 11 }

        // Right arm
        PathMove { x: 15; y: 11 }
        PathLine { x: 21; y: 11 }
        PathLine { x: 21; y: 13 }
        PathLine { x: 15; y: 13 }
        PathLine { x: 15; y: 11 }
    }
}
