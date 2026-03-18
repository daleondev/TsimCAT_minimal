qt_add_library(TsimCAT_UI STATIC)

target_compile_features(TsimCAT_UI PUBLIC cxx_std_23)
target_link_libraries(TsimCAT_UI PRIVATE Qt6::Quick Qt6::Quick3D Qt6::Quick3DAssetImport)

qt_add_qml_module(TsimCAT_UI
    URI "TsimCAT.UI"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/TsimCAT/UI"
    QML_FILES
        Main.qml
        controls/NavigationSidebar.qml
        controls/FenceModel.qml
        controls/GripperModel.qml
        controls/PartModel.qml
        controls/RobotModel.qml
        controls/Plant3DView.qml
        controls/RotaryTableModel.qml
        controls/ConveyorModel.qml
        icons/DashboardIcon.qml
        icons/RobotIcon.qml
        icons/AdsIcon.qml
        subscreens/PlantOverview.qml
        subscreens/RobotStatus.qml
        subscreens/AdsConfig.qml
    RESOURCES
        qtquickcontrols2.conf
        assets/meshes/base_link/Base_link.qml
        assets/meshes/base_link/meshes/node3.mesh
        assets/meshes/link_1/Link_1.qml
        assets/meshes/link_1/meshes/node3.mesh
        assets/meshes/link_2/Link_2.qml
        assets/meshes/link_2/meshes/node3.mesh
        assets/meshes/link_3/Link_3.qml
        assets/meshes/link_3/meshes/node3.mesh
        assets/meshes/link_4/Link_4.qml
        assets/meshes/link_4/meshes/node3.mesh
        assets/meshes/link_5/Link_5.qml
        assets/meshes/link_5/meshes/node3.mesh
        assets/meshes/link_6/Link_6.qml
        assets/meshes/link_6/meshes/node3.mesh
)
