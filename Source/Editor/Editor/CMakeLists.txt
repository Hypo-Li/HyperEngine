file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

set(AllFiles ${HEADER_FILES} ${SOURCE_FILES})

foreach(fileItem ${AllFiles})
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    set(GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

add_executable (Editor ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries (Editor Core Engine RenderBase Renderer AssetInterchange)
