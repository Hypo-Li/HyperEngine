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

add_executable (RenderTest ${HEADER_FILES} ${SOURCE_FILES})

target_link_libraries (RenderTest Engine AssetInterchange)

set(SLANG_DLL "$<IF:$<CONFIG:Debug>,${PROJECT_BINARY_DIR}/vcpkg_installed/x64-windows/debug/bin/slang-glslang.dll,${PROJECT_BINARY_DIR}/vcpkg_installed/x64-windows/bin/slang-glslang.dll>")

set(ISPC_DLL "$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/ThirdParty/ispc_texcomp/debug/bin/ispc_texcomp.dll,${CMAKE_SOURCE_DIR}/ThirdParty/ispc_texcomp/bin/ispc_texcomp.dll>")

add_custom_command(
    TARGET RenderTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANG_DLL}" "$<TARGET_FILE_DIR:RenderTest>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ISPC_DLL}" "$<TARGET_FILE_DIR:RenderTest>"
    COMMENT "Copying runtime dlls ($<CONFIG>) to build directory..."
)
