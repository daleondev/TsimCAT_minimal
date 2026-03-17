cmake_minimum_required(VERSION 4.0.0)
project(TsimCAT 
    VERSION 0.1 
    LANGUAGES CXX
)

set(TSIMCAT_ADS_DRIVER "AdsLib" CACHE STRING "ADS driver implementation to build")
set_property(CACHE TSIMCAT_ADS_DRIVER PROPERTY STRINGS AdsLib TcAdsDll)

set(_tsimcat_supported_ads_drivers AdsLib TcAdsDll)
list(FIND _tsimcat_supported_ads_drivers "${TSIMCAT_ADS_DRIVER}" _tsimcat_ads_driver_index)
if(_tsimcat_ads_driver_index EQUAL -1)
    message(FATAL_ERROR "Unsupported TSIMCAT_ADS_DRIVER='${TSIMCAT_ADS_DRIVER}'. Expected one of: ${_tsimcat_supported_ads_drivers}")
endif()

message(STATUS "TsimCAT ADS driver: ${TSIMCAT_ADS_DRIVER}")

if(NOT WIN32 OR NOT MSVC)
	message(FATAL_ERROR "TsimCAT is Windows only and requires MSVC toolchain.")
endif()

set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)
find_package(Qt6 REQUIRED COMPONENTS Quick Quick3D Quick3DAssetImport QuickControls2)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
add_compile_options(/constexpr:steps20000000)

qt_standard_project_setup()

include(third_party/third_party.cmake)

add_subdirectory(src)