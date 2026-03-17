add_library(core_utils INTERFACE)
add_library(core::utils ALIAS core_utils)

target_sources(
    core_utils
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/..
    FILES 
        memory_utils.hpp
        queue_utils.hpp
)

target_compile_features(
    core_utils 
    INTERFACE
    cxx_std_23
)
