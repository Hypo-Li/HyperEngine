function(add_smart_library TARGET_NAME)
    file(GLOB_RECURSE HEADER_FILES "*.h" "*.hpp")
    file(GLOB_RECURSE INLINE_FILES "*.inl")
    file(GLOB_RECURSE SOURCE_FILES "*.cpp" "*.c")

    set(ALL_FILES ${HEADER_FILES} ${INLINE_FILES} ${SOURCE_FILES})

    foreach (FILE_ITEM ${ALL_FILES})
        file(RELATIVE_PATH REL_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${FILE_ITEM}")
        get_filename_component(PARENT_DIR "${REL_PATH}" DIRECTORY)

        if (PARENT_DIR)
            string(REPLACE "/" "\\" GROUP_NAME "${PARENT_DIR}")
            source_group("${GROUP_NAME}" FILES "${FILE_ITEM}")
        else()
            source_group("" FILES "${FILE_ITEM}")
        endif()
    endforeach()

    add_library(${TARGET_NAME} ${ALL_FILES})
    
    set(HYPER_ENGINE_NATVIS "${CMAKE_SOURCE_DIR}/Script/Visualizer/HyperEngine.natvis")
    source_group("Visualizer" FILES "${HYPER_ENGINE_NATVIS}")
    target_sources(${TARGET_NAME} PUBLIC "${HYPER_ENGINE_NATVIS}")
endfunction()

include_directories(
    "${CMAKE_SOURCE_DIR}/Source/Runtime"
)

add_subdirectory("Core")
add_subdirectory("Engine")
add_subdirectory("Platform")
add_subdirectory("RenderBase")
add_subdirectory("Renderer")
add_subdirectory("PyHyperEngine")

set(RUNTIME_TARGETS Core Engine Platform RenderBase Renderer PyHyperEngine PyHyperEngineStatic)
set_property(TARGET ${RUNTIME_TARGETS} PROPERTY FOLDER "Runtime")
