cmake_minimum_required(VERSION 4.2.3)
project(dds_image LANGUAGES CXX)

add_library(dds_image INTERFACE dds.hpp dds_formats.hpp)
add_library(dds_image::dds_image ALIAS dds_image)

target_include_directories(dds_image
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(dds_image PROPERTIES
    EXPORT_NAME dds_image
)
