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
    source_group("Natvis" FILES "${HYPER_ENGINE_NATVIS}")
    target_sources(${TARGET_NAME} PUBLIC "${HYPER_ENGINE_NATVIS}")

    # nlohmann_json 自带一份 natvis(vcpkg 装到 <nlohmann_json_DIR>/nlohmann_json.natvis),
    # vcpkg 导出的 nlohmann_jsonTargets.cmake 把它挂在 nlohmann_json::nlohmann_json 这个
    # imported target 的 INTERFACE_SOURCES 上——Core PUBLIC 链接了它, 所以任何链接 Core
    # (直接或间接)的 target 都会自动带上这个文件当自己的源文件之一, 不需要我们手动
    # target_sources; 缺的只是一个 source_group, 不然 VS 里就会和真正的源码摆在一起。
    # 用 nlohmann_json_DIR(find_package 产出的 CACHE 变量, 不受目录作用域限制, 每个调用
    # add_smart_library 的目录都能读到)拼路径, 不写死 vcpkg triplet。
    if (DEFINED nlohmann_json_DIR)
        set(NLOHMANN_JSON_NATVIS "${nlohmann_json_DIR}/nlohmann_json.natvis")
        if (EXISTS "${NLOHMANN_JSON_NATVIS}")
            source_group("Natvis" FILES "${NLOHMANN_JSON_NATVIS}")
        endif()
    endif()
endfunction()
