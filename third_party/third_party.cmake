set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# QCoro
set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTNETWORK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/qcoro)

# ADS
set(TSIMCAT_ADS_RUNTIME_DLL "")

if(TSIMCAT_ADS_DRIVER STREQUAL "AdsLib")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/ADS/AdsLib)
    set_target_properties(ads PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ads")
    target_compile_definitions(ads PRIVATE CONFIG_DEFAULT_LOGLEVEL=1)

    add_library(ads_driver INTERFACE)
    target_link_libraries(ads_driver INTERFACE ads::ads)
elseif(TSIMCAT_ADS_DRIVER STREQUAL "TcAdsDll")
    set(_tcadsdll_root "${CMAKE_CURRENT_LIST_DIR}/TcAdsDll")
    set(_tcadsdll_include_dir "${_tcadsdll_root}/Include")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_tcadsdll_implib "${_tcadsdll_root}/x64/lib/TcAdsDll.lib")
        set(_tcadsdll_runtime "${_tcadsdll_root}/x64/TcAdsDll.dll")
    else()
        set(_tcadsdll_implib "${_tcadsdll_root}/Lib/TcAdsDll.lib")
        set(_tcadsdll_runtime "${_tcadsdll_root}/TcAdsDll.dll")
    endif()

    if(NOT EXISTS "${_tcadsdll_include_dir}/TcAdsAPI.h")
        message(FATAL_ERROR "TcAdsDll headers were not found under ${_tcadsdll_include_dir}")
    endif()

    if(NOT EXISTS "${_tcadsdll_implib}")
        message(FATAL_ERROR "TcAdsDll import library was not found at ${_tcadsdll_implib}")
    endif()

    if(NOT EXISTS "${_tcadsdll_runtime}")
        message(FATAL_ERROR "TcAdsDll runtime was not found at ${_tcadsdll_runtime}")
    endif()

    add_library(tcadsdll SHARED IMPORTED GLOBAL)
    set_target_properties(tcadsdll PROPERTIES
        IMPORTED_IMPLIB "${_tcadsdll_implib}"
        IMPORTED_LOCATION "${_tcadsdll_runtime}"
        INTERFACE_INCLUDE_DIRECTORIES "${_tcadsdll_include_dir}"
    )
    add_library(TcAdsDll::TcAdsDll ALIAS tcadsdll)

    add_library(ads_driver INTERFACE)
    target_link_libraries(ads_driver INTERFACE TcAdsDll::TcAdsDll)

    set(TSIMCAT_ADS_RUNTIME_DLL "${_tcadsdll_runtime}")
else()
    message(FATAL_ERROR "Unsupported TSIMCAT_ADS_DRIVER='${TSIMCAT_ADS_DRIVER}'")
endif()

add_library(ads::driver ALIAS ads_driver)

# Open62541
set(UA_ENABLE_AMALGAMATION ON CACHE BOOL "" FORCE)
set(UA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(UA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(UA_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/open62541)

# asio
add_library(asio INTERFACE)
add_library(asio::asio ALIAS asio)
target_include_directories(
    asio
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/asio/include
)

# Eigen
find_package(Eigen3 REQUIRED)
# vcpkg provides Eigen3::Eigen

# KDL
# KDL's FindEigen3.cmake or internal logic might need help finding the vcpkg Eigen
get_target_property(EIGEN3_INCLUDE_DIR Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
set(EIGEN3_INCLUDE_DIR ${EIGEN3_INCLUDE_DIR} CACHE PATH "" FORCE) 

set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(KDL_USE_NEW_TREE_INTERFACE OFF CACHE BOOL "" FORCE) # Avoid Boost dependency
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/kdl/orocos_kdl)
# Force KDL to be static and use our Eigen
set_target_properties(orocos-kdl PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(orocos-kdl PRIVATE Eigen3::Eigen)
target_include_directories(orocos-kdl PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/kdl/orocos_kdl/src>")

# OMPL
set(OMPL_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(OMPL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OMPL_BUILD_PYBINDINGS OFF CACHE BOOL "" FORCE)
set(OMPL_BUILD_PYTESTS OFF CACHE BOOL "" FORCE)
set(OMPL_VERSIONED_INSTALL OFF CACHE BOOL "" FORCE)
set(OMPL_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# If flann failed to build, we ensure OMPL doesn't try to find it
set(OMPL_HAVE_FLANN OFF CACHE BOOL "" FORCE) 

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/ompl)
# OMPL target is "ompl"
if(TARGET ompl)
    target_compile_definitions(ompl PRIVATE _USE_MATH_DEFINES)
endif()