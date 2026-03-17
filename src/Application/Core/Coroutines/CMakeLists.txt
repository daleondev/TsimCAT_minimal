add_library(core_coroutines INTERFACE)
add_library(core::coroutines ALIAS core_coroutines)

target_sources(
    core_coroutines
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/..
    FILES 
        coroutine.hpp
        Context.hpp
        Task.hpp
        Channel.hpp
)

target_link_libraries(
    core_coroutines
    INTERFACE
    core::common
    core::utils
)

target_compile_features(
    core_coroutines 
    INTERFACE 
    cxx_std_23
)
