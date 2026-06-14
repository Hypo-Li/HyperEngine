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

find_package(Python COMPONENTS Interpreter Development)
find_package(pybind11 CONFIG REQUIRED)

add_library(PyHyperEngineStatic STATIC ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(PyHyperEngineStatic PRIVATE pybind11::embed Engine)
target_compile_definitions(PyHyperEngineStatic PRIVATE BUILDING_EMBEDDED)

Python_add_library(PyHyperEngine ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(PyHyperEngine PRIVATE pybind11::headers Engine)

# if(MSVC)
#    set_target_properties(PyHyperEngine PROPERTIES 
#        DEBUG_POSTFIX "_d"
#    )
# endif()
