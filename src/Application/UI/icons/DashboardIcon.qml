import QtQuick
import QtQuick.Shapes

Shape {
    id: iconRoot
    property color color: "white"
    width: 24
    height: 24
    anchors.centerIn: parent
    antialiasing: true
    ShapePath {
        fillColor: iconRoot.color
        strokeColor: "transparent"
        PathMove { x: 3; y: 3 }
        PathLine { x: 11; y: 3 }
        PathLine { x: 11; y: 11 }
        PathLine { x: 3; y: 11 }
        PathLine { x: 3; y: 3 }
        PathMove { x: 13; y: 3 }
        PathLine { x: 21; y: 3 }
        PathLine { x: 21; y: 11 }
        PathLine { x: 13; y: 11 }
        PathLine { x: 13; y: 3 }
        PathMove { x: 3; y: 13 }
        PathLine { x: 11; y: 13 }
        PathLine { x: 11; y: 21 }
        PathLine { x: 3; y: 21 }
        PathLine { x: 3; y: 13 }
        PathMove { x: 13; y: 13 }
        PathLine { x: 21; y: 13 }
        PathLine { x: 21; y: 21 }
        PathLine { x: 13; y: 21 }
        PathLine { x: 13; y: 13 }
    }
}
