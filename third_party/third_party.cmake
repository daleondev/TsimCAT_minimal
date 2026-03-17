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