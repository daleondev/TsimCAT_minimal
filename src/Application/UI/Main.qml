import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TsimCAT.Backend 1.0 as BackendModule
import "controls" as Controls
import "subscreens" as Subscreens

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    title: qsTr("Simulation")

    BackendModule.Backend {
        id: backend
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Navigation Bar
        Controls.NavigationSidebar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 240
            footerText: "© 2026 Simulation"

            model: [
                {
                    name: "Plant Overview",
                    icon: "DashboardIcon"
                },
                {
                    name: "ADS Config",
                    icon: "AdsIcon"
                }
            ]
        }

        // Main Content Area
        StackLayout {
            id: contentStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: sidebar.currentIndex

            Subscreens.PlantOverview {
                backend: backend
            }
            Subscreens.AdsConfig {
                backend: backend
            }
        }
    }
}
