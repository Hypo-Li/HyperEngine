include("${CMAKE_SOURCE_DIR}/CMake/AddSmartLibrary.cmake")

include_directories(
    "${CMAKE_SOURCE_DIR}/Source/Editor"
    "${CMAKE_SOURCE_DIR}/Source/Runtime"
)

add_subdirectory("AssetInterchange")
add_subdirectory("Editor")

set(EDITOR_TARGETS AssetInterchange Editor)
set_property(TARGET ${EDITOR_TARGETS} PROPERTY FOLDER "Editor")
