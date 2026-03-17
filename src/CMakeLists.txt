add_subdirectory(Application)

qt_add_executable(appTsimCAT
    main.cpp
)

target_compile_features(
    appTsimCAT 
    PRIVATE 
    cxx_std_23
)

target_link_libraries(
    appTsimCAT
    PRIVATE 
    Qt6::Quick
    Qt6::QuickControls2
    TsimCAT_UI
    TsimCAT_UIplugin
    TsimCAT_Backend
    TsimCAT_Backendplugin
)

set(_vcpkg_installed_dir "${VCPKG_INSTALLED_DIR}")
if(NOT _vcpkg_installed_dir AND DEFINED CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_vcpkg_scripts_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
    get_filename_component(_vcpkg_root "${_vcpkg_scripts_dir}/../.." ABSOLUTE)
    set(_vcpkg_installed_dir "${_vcpkg_root}/installed")
endif()

set(_vcpkg_triplet "${VCPKG_TARGET_TRIPLET}")
if(NOT _vcpkg_triplet)
    set(_vcpkg_triplet "x64-windows")
endif()

set(_runtime_bin "${_vcpkg_installed_dir}/${_vcpkg_triplet}/$<IF:$<CONFIG:Debug>,debug/bin,bin>")
set(_qt_plugins "${_vcpkg_installed_dir}/${_vcpkg_triplet}/$<IF:$<CONFIG:Debug>,debug/Qt6/plugins,Qt6/plugins>")
set(_qt_qml "${_vcpkg_installed_dir}/${_vcpkg_triplet}/$<IF:$<CONFIG:Debug>,debug/Qt6/qml,Qt6/qml>")

set(_runtime_path "${_runtime_bin}$<SEMICOLON>$ENV{PATH}")

set_property(TARGET appTsimCAT PROPERTY VS_DEBUGGER_ENVIRONMENT
    "PATH=${_runtime_path}\nQT_PLUGIN_PATH=${_qt_plugins}\nQML2_IMPORT_PATH=${_qt_qml}")

add_custom_target(run_appTsimCAT
    COMMAND ${CMAKE_COMMAND} -E env
        "PATH=${_runtime_path}"
        "QT_PLUGIN_PATH=${_qt_plugins}"
        "QML2_IMPORT_PATH=${_qt_qml}"
        "$<TARGET_FILE:appTsimCAT>"
    DEPENDS appTsimCAT
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:appTsimCAT>"
    VERBATIM
)

file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/run_appTsimCAT_$<CONFIG>.ps1"
    CONTENT "Set-Location '$<TARGET_FILE_DIR:appTsimCAT>'\n$env:PATH='${_runtime_path}'\n$env:QT_PLUGIN_PATH='${_qt_plugins}'\n$env:QML2_IMPORT_PATH='${_qt_qml}'\n& '$<TARGET_FILE:appTsimCAT>'\n")

if(TSIMCAT_ADS_RUNTIME_DLL)
    add_custom_command(TARGET appTsimCAT POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TSIMCAT_ADS_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:appTsimCAT>/TcAdsDll.dll"
    )
endif()