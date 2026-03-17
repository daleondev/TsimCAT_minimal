add_library(core_link)
add_library(core::link ALIAS core_link)

set(_ads_client_source Symbolic/AdsClient.cpp)
set(_ads_driver_compile_definition TSIMCAT_ADS_DRIVER_ADSLIB=1)

if(TSIMCAT_ADS_DRIVER STREQUAL "TcAdsDll")
    set(_ads_client_source Symbolic/TcAdsDllClient.cpp)
    set(_ads_driver_compile_definition TSIMCAT_ADS_DRIVER_TCADSDLL=1)
endif()

target_sources(
    core_link
    PRIVATE
    LinkFactory.cpp
    Raw/TcpServer.cpp
    ${_ads_client_source}
    Symbolic/LocalAdsLink.cpp
    Symbolic/OpcUaClient.cpp

    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/..
    FILES 
        ILink.hpp
        LinkFactory.hpp
        Subscription.hpp
        Raw/IRawLink.hpp
        Symbolic/ISymbolicLink.hpp
        Symbolic/LocalAdsLink.hpp
)

target_link_libraries(
    core_link
    PRIVATE
    asio::asio
    ads::driver
    open62541::open62541
    ws2_32
    mswsock

    PUBLIC
    core::common
    core::coroutines
)

target_include_directories(
    core_link
    PRIVATE
    ${CMAKE_BINARY_DIR}/third_party/open62541
)

target_compile_definitions(
    core_link
    PUBLIC
    ASIO_STANDALONE

    PRIVATE
    ${_ads_driver_compile_definition}
)

target_compile_features(
    core_link 
    PUBLIC 
    cxx_std_23
)
