qt_add_library(TsimCAT_Backend STATIC)

target_link_libraries(TsimCAT_Backend 
    PUBLIC 
    Qt6::Quick
    QCoro6::Core
    QCoro6::Quick

    PRIVATE
    core::link
)

target_compile_features(
    TsimCAT_Backend 
    PUBLIC 
    cxx_std_23
)

qt_add_qml_module(TsimCAT_Backend
    URI "TsimCAT.Backend"
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/TsimCAT/Backend"
    SOURCES
        AdsConfigBackend.h
        AdsConfigBackend.cpp
        ConveyorBackend.h
        ConveyorBackend.cpp
        RotaryTableBackend.h
        RotaryTableBackend.cpp
        Backend.h
        Backend.cpp
)