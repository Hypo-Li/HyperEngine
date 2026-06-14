cmake_minimum_required(VERSION 3.10)
project(vk_radix_sort LANGUAGES C)

add_library(vk_radix_sort INTERFACE vk_radix_sort.h)
add_library(vk_radix_sort::vk_radix_sort ALIAS vk_radix_sort)

target_include_directories(vk_radix_sort
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(vk_radix_sort INTERFACE VRDX_USE_VOLK)

target_link_libraries(vk_radix_sort INTERFACE volk::volk)

set_target_properties(vk_radix_sort PROPERTIES
    EXPORT_NAME vk_radix_sort
)
