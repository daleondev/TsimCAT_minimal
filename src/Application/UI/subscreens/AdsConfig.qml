import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: root
    required property var backend

    property var ads: backend.adsConfig
    property bool compactHero: page.width < 900
    property color ink: "#173042"
    property color mutedInk: "#5c7080"
    property color cardBorder: "#d7e1e7"
    property color connectionColor: {
        if (ads.connectionStateLabel === "Connected") {
            return "#2f8f5b"
        }
        if (ads.connectionStateLabel === "Connecting") {
            return "#d88a1a"
        }
        if (ads.connectionStateLabel === "Error") {
            return "#c54b3c"
        }
        return "#8796a3"
    }

    background: Rectangle {
        color: "#eef4f7"

        Rectangle {
            width: parent.width * 0.55
            height: width
            radius: width / 2
            x: parent.width - width * 0.55
            y: -height * 0.35
            color: "#d8e8f0"
            opacity: 0.65
        }

        Rectangle {
            width: parent.width * 0.42
            height: width
            radius: width / 2
            x: -width * 0.2
            y: parent.height - height * 0.55
            color: "#f1e8dc"
            opacity: 0.8
        }

        Gradient {
            id: backdropGradient
            GradientStop {
                position: 0.0
                color: "#f7f2e8"
            }
            GradientStop {
                position: 1.0
                color: "#edf5f8"
            }
        }

        Rectangle {
            anchors.fill: parent
            gradient: backdropGradient
            opacity: 0.84
        }
    }

    contentItem: ScrollView {
        id: scroll
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        contentWidth: availableWidth

        Item {
            width: scroll.availableWidth
            implicitHeight: page.implicitHeight + 56

            Column {
                id: page
                width: Math.min(Math.max(scroll.availableWidth - 48, 320), 1180)
                x: Math.max(24, (scroll.availableWidth - width) / 2)
                y: 28
                spacing: 24

                Rectangle {
                    id: heroCard
                    width: parent.width
                    radius: 28
                    color: "#173d5c"
                    border.color: "#23557d"
                    border.width: 1
                    implicitHeight: heroContent.implicitHeight + 52
                    clip: true

                    Rectangle {
                        width: 220
                        height: 220
                        radius: 110
                        x: parent.width - width * 0.72
                        y: -42
                        color: "#24567b"
                        opacity: 0.55
                    }

                    Rectangle {
                        width: 140
                        height: 140
                        radius: 70
                        x: parent.width - width * 1.18
                        y: 54
                        color: "#2d6c95"
                        opacity: 0.35
                    }

                    Column {
                        id: heroContent
                        anchors.fill: parent
                        anchors.margins: 26
                        spacing: 12

                        Rectangle {
                            radius: 13
                            color: "#2a5f86"
                            height: 30
                            width: heroTag.implicitWidth + 24

                            Label {
                                id: heroTag
                                anchors.centerIn: parent
                                text: qsTr("TwinCAT ADS Endpoint")
                                color: "#d7edf8"
                                font.pixelSize: 13
                                font.bold: true
                            }
                        }

                        Label {
                            text: qsTr("ADS Configuration")
                            color: "#fff8ef"
                            font.pixelSize: root.compactHero ? 34 : 44
                            font.bold: true
                        }

                        Label {
                            width: parent.width * (root.compactHero ? 0.9 : 0.68)
                            text: qsTr("Connect to a PLC, persist your endpoint defaults, and read or write a symbol from one workspace-friendly screen.")
                            wrapMode: Text.WordWrap
                            color: "#d3e5f1"
                            font.pixelSize: root.compactHero ? 15 : 16
                        }
                    }
                }

                GridLayout {
                    id: grid
                    width: parent.width
                    columns: width >= 940 ? 2 : 1
                    rowSpacing: 22
                    columnSpacing: 22

                    Rectangle {
                        id: connectionCard
                        Layout.fillWidth: true
                        Layout.preferredWidth: grid.columns === 2 ? (grid.width - grid.columnSpacing) / 2 : grid.width
                        Layout.alignment: Qt.AlignTop
                        color: "#fffdf9"
                        radius: 24
                        border.color: root.cardBorder
                        border.width: 1
                        implicitHeight: connectionCardContent.implicitHeight + 48

                        Column {
                            id: connectionCardContent
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 20

                            Row {
                                width: parent.width
                                spacing: 14

                                Rectangle {
                                    width: 54
                                    height: 54
                                    radius: 18
                                    color: Qt.tint(root.connectionColor, "#20ffffff")
                                    border.color: Qt.tint(root.connectionColor, "#55ffffff")
                                    border.width: 1

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 14
                                        height: 14
                                        radius: 7
                                        color: root.connectionColor
                                    }
                                }

                                Column {
                                    width: parent.width - 68
                                    spacing: 5

                                    Label {
                                        text: qsTr("Connection")
                                        font.pixelSize: 28
                                        font.bold: true
                                        color: root.ink
                                    }

                                    Label {
                                        text: qsTr("Current state: %1").arg(ads.connectionStateLabel)
                                        color: root.mutedInk
                                    }
                                }
                            }

                            RowLayout {
                                width: parent.width
                                spacing: 12

                                Button {
                                    Layout.preferredWidth: 150
                                    Layout.preferredHeight: 42
                                    text: qsTr("Connect")
                                    enabled: ads.canConnect
                                    font.bold: true
                                    onClicked: ads.connectToAds()
                                }

                                Button {
                                    Layout.preferredWidth: 150
                                    Layout.preferredHeight: 42
                                    text: qsTr("Disconnect")
                                    enabled: ads.canDisconnect
                                    onClicked: ads.disconnectFromAds()
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: 14
                                    color: ads.dirty ? "#fff0d7" : "#e6f4e8"
                                    border.color: ads.dirty ? "#e7c07d" : "#a9d0ae"
                                    border.width: 1
                                    implicitWidth: stateLabel.implicitWidth + 18
                                    implicitHeight: 30

                                    Label {
                                        id: stateLabel
                                        anchors.centerIn: parent
                                        text: ads.dirty ? qsTr("Unsaved") : qsTr("Saved")
                                        color: ads.dirty ? "#9c6420" : "#3f744b"
                                        font.bold: true
                                        font.pixelSize: 13
                                    }
                                }
                            }

                            Rectangle {
                                id: connectionFieldsCard
                                width: parent.width
                                color: "#f7fafc"
                                radius: 20
                                border.color: "#dde7ec"
                                border.width: 1
                                implicitHeight: connectionFields.implicitHeight + 40

                                GridLayout {
                                    id: connectionFields
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    columns: width >= 520 ? 2 : 1
                                    columnSpacing: 18
                                    rowSpacing: 16

                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: qsTr("Server IP Address")
                                            color: root.ink
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Default 127.0.0.1")
                                            color: root.mutedInk
                                            font.pixelSize: 12
                                        }

                                        TextField {
                                            width: parent.width
                                            text: ads.serverIp
                                            placeholderText: "127.0.0.1"
                                            onTextEdited: ads.serverIp = text
                                        }
                                    }

                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: qsTr("ADS Server Net ID")
                                            color: root.ink
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Default 127.0.0.1.1.1")
                                            color: root.mutedInk
                                            font.pixelSize: 12
                                        }

                                        TextField {
                                            width: parent.width
                                            text: ads.serverNetId
                                            placeholderText: "127.0.0.1.1.1"
                                            onTextEdited: ads.serverNetId = text
                                        }
                                    }

                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: qsTr("Local Net ID")
                                            color: root.ink
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Default 127.0.0.1.1.20")
                                            color: root.mutedInk
                                            font.pixelSize: 12
                                        }

                                        TextField {
                                            width: parent.width
                                            text: ads.localNetId
                                            placeholderText: "127.0.0.1.1.20"
                                            onTextEdited: ads.localNetId = text
                                        }
                                    }

                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: qsTr("ADS Port")
                                            color: root.ink
                                            font.bold: true
                                        }

                                        Label {
                                            text: qsTr("Default 851")
                                            color: root.mutedInk
                                            font.pixelSize: 12
                                        }

                                        TextField {
                                            width: parent.width
                                            text: String(ads.adsPort)
                                            placeholderText: "851"
                                            validator: IntValidator {
                                                bottom: 1
                                                top: 65535
                                            }
                                            onEditingFinished: {
                                                if (acceptableInput && text.length > 0) {
                                                    ads.adsPort = Number(text)
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                width: parent.width
                                spacing: 12

                                Button {
                                    Layout.preferredWidth: 140
                                    Layout.preferredHeight: 42
                                    text: qsTr("Save Changes")
                                    enabled: ads.canSave
                                    font.bold: true
                                    onClicked: ads.saveConfig()
                                }

                                Button {
                                    Layout.preferredWidth: 140
                                    Layout.preferredHeight: 42
                                    text: qsTr("Discard")
                                    enabled: ads.canDiscard
                                    onClicked: ads.discardChanges()
                                }
                            }
                        }
                    }

                    Rectangle {
                        id: variableCard
                        Layout.fillWidth: true
                        Layout.preferredWidth: grid.columns === 2 ? (grid.width - grid.columnSpacing) / 2 : grid.width
                        Layout.alignment: Qt.AlignTop
                        color: "#fbfdff"
                        radius: 24
                        border.color: root.cardBorder
                        border.width: 1
                        implicitHeight: variableCardContent.implicitHeight + 48

                        Column {
                            id: variableCardContent
                            anchors.fill: parent
                            anchors.margins: 24
                            spacing: 20

                            Column {
                                width: parent.width
                                spacing: 6

                                Label {
                                    text: qsTr("Variable Access")
                                    font.pixelSize: 28
                                    font.bold: true
                                    color: root.ink
                                }

                                Label {
                                    width: parent.width
                                    text: qsTr("Read or write one ADS symbol using the active connection. Use the type selector to control parsing and formatting.")
                                    wrapMode: Text.WordWrap
                                    color: root.mutedInk
                                }
                            }

                            Rectangle {
                                id: variableAccessCard
                                width: parent.width
                                color: "#f5fafe"
                                radius: 20
                                border.color: "#dce8f1"
                                border.width: 1
                                implicitHeight: variableAccessContent.implicitHeight + 40

                                Column {
                                    id: variableAccessContent
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 16

                                    Column {
                                        width: parent.width
                                        spacing: 6

                                        Label {
                                            text: qsTr("Variable Name")
                                            color: root.ink
                                            font.bold: true
                                        }

                                        TextField {
                                            width: parent.width
                                            text: ads.variableName
                                            placeholderText: qsTr("MAIN.sampleSymbol")
                                            onTextEdited: ads.variableName = text
                                        }
                                    }

                                    Row {
                                        width: parent.width
                                        spacing: 14

                                        Column {
                                            width: Math.max(120, parent.width * 0.38)
                                            spacing: 6

                                            Label {
                                                text: qsTr("Type")
                                                color: root.ink
                                                font.bold: true
                                            }

                                            ComboBox {
                                                width: parent.width
                                                model: ads.variableTypes
                                                currentIndex: Math.max(0, ads.variableTypes.indexOf(ads.variableType))
                                                onActivated: ads.variableType = currentText
                                            }
                                        }

                                        Column {
                                            width: parent.width - Math.max(120, parent.width * 0.38) - 14
                                            spacing: 6

                                            Label {
                                                text: qsTr("Value")
                                                color: root.ink
                                                font.bold: true
                                            }

                                            TextField {
                                                width: parent.width
                                                text: ads.variableValue
                                                placeholderText: qsTr("Enter a value or read from ADS")
                                                onTextEdited: ads.variableValue = text
                                            }
                                        }
                                    }

                                    RowLayout {
                                        width: parent.width
                                        spacing: 12

                                        Button {
                                            Layout.preferredWidth: 120
                                            Layout.preferredHeight: 42
                                            text: qsTr("Read")
                                            enabled: ads.connected
                                            font.bold: true
                                            onClicked: ads.readVariable()
                                        }

                                        Button {
                                            Layout.preferredWidth: 120
                                            Layout.preferredHeight: 42
                                            text: qsTr("Write")
                                            enabled: ads.connected
                                            font.bold: true
                                            onClicked: ads.writeVariable()
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Label {
                                            text: ads.connected ? qsTr("Ready") : qsTr("Connect to enable I/O")
                                            color: ads.connected ? "#4d7156" : root.mutedInk
                                            font.bold: true
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                id: statusCard
                                width: parent.width
                                radius: 18
                                color: ads.statusIsError ? "#fdf0ed" : "#edf7f4"
                                border.color: ads.statusIsError ? "#f0beb7" : "#bfddcf"
                                border.width: 1
                                implicitHeight: statusContent.implicitHeight + 40

                                Column {
                                    id: statusContent
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 10

                                    Row {
                                        spacing: 10

                                        Rectangle {
                                            width: 10
                                            height: 10
                                            radius: 5
                                            color: ads.statusIsError ? "#c54b3c" : "#338466"
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        Label {
                                            text: ads.statusIsError ? qsTr("Status · Attention") : qsTr("Status · Ready")
                                            color: ads.statusIsError ? "#9c3528" : "#2d6954"
                                            font.bold: true
                                        }
                                    }

                                    Label {
                                        width: parent.width
                                        text: ads.statusMessage.length > 0
                                              ? ads.statusMessage
                                              : qsTr("Load the saved endpoint, connect to the controller, then use Read and Write to test symbol access.")
                                        wrapMode: Text.WordWrap
                                        color: ads.statusIsError ? "#8f3024" : "#335f52"
                                        lineHeight: 1.2
                                    }
                                }
                            }

                            Rectangle {
                                id: snapshotCard
                                width: parent.width
                                radius: 18
                                color: "#173042"
                                border.color: "#2c5167"
                                border.width: 1
                                implicitHeight: snapshotContent.implicitHeight + 40

                                Row {
                                    id: snapshotContent
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 16

                                    Rectangle {
                                        width: 48
                                        height: 48
                                        radius: 16
                                        color: Qt.tint(root.connectionColor, "#33ffffff")
                                        border.color: Qt.tint(root.connectionColor, "#55ffffff")
                                        border.width: 1

                                        Rectangle {
                                            anchors.centerIn: parent
                                            width: 14
                                            height: 14
                                            radius: 7
                                            color: root.connectionColor
                                        }
                                    }

                                    Column {
                                        width: parent.width - 64
                                        spacing: 6

                                        Label {
                                            text: qsTr("Session Snapshot")
                                            color: "#f7f3ea"
                                            font.bold: true
                                            font.pixelSize: 18
                                        }

                                        Label {
                                            width: parent.width
                                            text: qsTr("Endpoint %1 · %2 · Port %3")
                                                    .arg(ads.serverIp)
                                                    .arg(ads.serverNetId)
                                                    .arg(ads.adsPort)
                                            color: "#d0e2ee"
                                            wrapMode: Text.WordWrap
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
