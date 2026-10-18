include("${CMAKE_SOURCE_DIR}/CMake/AddSmartLibrary.cmake")

include_directories(
    "${CMAKE_SOURCE_DIR}/Source/Editor"
    "${CMAKE_SOURCE_DIR}/Source/Runtime"
)

add_subdirectory("AssetInterchange")
add_subdirectory("FontUtility")
add_subdirectory("LanguageIntelligence")
add_subdirectory("HypImGui")
add_subdirectory("MeshUtility")
add_subdirectory("ShaderCompiler")
add_subdirectory("TextureUtility")
add_subdirectory("Editor")

set(EDITOR_TARGETS AssetInterchange FontUtility LanguageIntelligence HypImGui MeshUtility ShaderCompiler TextureUtility Editor)
set_property(TARGET ${EDITOR_TARGETS} PROPERTY FOLDER "Editor")
