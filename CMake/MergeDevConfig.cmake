# 以 cmake -P 脚本模式运行, 由 GenerateDevConfig.cmake 的 POST_BUILD 命令调用。
# 只更新 CONFIG_PATH 指向的 config.json 里的这 4 个目录字段, 其它字段(比如 Editor
# 运行时写回的 DefaultScene)原样保留——不存在或解析不出来就当成空 {} 重新开始。
set(existing "{}")
if(EXISTS "${CONFIG_PATH}")
    file(READ "${CONFIG_PATH}" existing)
endif()

string(JSON existing SET "${existing}" "EngineAssetDir" "\"${ENGINE_ASSET_DIR}\"")
string(JSON existing SET "${existing}" "ProjectAssetDir" "\"${PROJECT_ASSET_DIR}\"")
string(JSON existing SET "${existing}" "ResourceDir" "\"${RESOURCE_DIR}\"")
string(JSON existing SET "${existing}" "ShaderDir" "\"${SHADER_DIR}\"")

file(WRITE "${CONFIG_PATH}" "${existing}")
