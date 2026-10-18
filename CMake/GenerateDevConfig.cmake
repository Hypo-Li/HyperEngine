# 给指定 target 在它的输出目录下维护一份指向源码树 Asset/Resource/Shader/Project 目录的
# config.json——引擎默认(没有 config.json,或者字段缺失)会把这些目录当成和可执行文件同目录的
# 相对路径解析, 这对"打包后随便挪到哪台机器都能跑"是对的, 但会破坏"直接跑 Build 目录里编出来的
# 可执行文件"这个开发时的日常工作流(源码树的 Asset/Shader 等目录并不在可执行文件旁边)。
# 这里维护的 config.json 只是开发期的便利, 真正打包分发时不应该带这一份(要么替换成指向实际
# 部署位置的相对路径版本, 要么把 Asset/Resource/Shader 拷到可执行文件旁边、不带 config.json,
# 走引擎的默认兜底路径)。
#
# 每次构建完这个 target 后跑 MergeDevConfig.cmake 合并写入, 而不是整份覆盖——config.json
# 里可能还有程序运行时自己写回的字段(比如 Editor 的 DefaultScene), 不该被这里冲掉。
function(generate_dev_config TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_PATH=$<TARGET_FILE_DIR:${TARGET_NAME}>/config.json
            -DENGINE_ASSET_DIR=${CMAKE_SOURCE_DIR}/Asset
            -DPROJECT_ASSET_DIR=${CMAKE_SOURCE_DIR}/Project/Asset
            -DRESOURCE_DIR=${CMAKE_SOURCE_DIR}/Resource
            -DSHADER_DIR=${CMAKE_SOURCE_DIR}/Shader
            -P "${CMAKE_SOURCE_DIR}/CMake/MergeDevConfig.cmake"
    )
endfunction()
