add_library(core_link)
add_library(core::link ALIAS core_link)

target_sources(
    core_link
    PRIVATE
    LinkFactory.cpp
    Raw/TcpServer.cpp
    Symbolic/AdsClient.cpp
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
    ads::ads
    open62541::open62541
    ws2_32
    mswsock

    PUBLIC
    core::common
    core::coroutines
)

target_compile_definitions(
    core_link
    PUBLIC
    ASIO_STANDALONE
)

target_compile_features(
    core_link 
    PUBLIC 
    cxx_std_23
)
