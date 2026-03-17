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
        PathMove { x: 12; y: 4 }
        PathArc { x: 12; y: 10; radiusX: 3; radiusY: 3; useLargeArc: true }
        PathArc { x: 12; y: 4; radiusX: 3; radiusY: 3; useLargeArc: true }
        PathMove { x: 6; y: 20 }
        PathLine { x: 18; y: 20 }
        PathLine { x: 18; y: 12 }
        PathLine { x: 6; y: 12 }
        PathLine { x: 6; y: 20 }
    }
}
