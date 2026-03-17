import QtQuick
import QtQuick.Controls
import TsimCAT.Backend 1.0 as BackendModule

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    title: qsTr("TsimCAT Control Center")

    BackendModule.Backend {
        id: backend
    }

    Text {
        text: backend.welcomeMessage
        anchors.centerIn: parent
        font.pixelSize: 24
    }
}
