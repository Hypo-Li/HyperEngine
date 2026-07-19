#pragma once
#include <vector>
#include <string>
#include <span>

#include "Shader.h"

namespace hyp
{
    class ShaderCompiler
    {
    public:
        enum class ShaderStage
        {
            Vertex,
            Fragment,
            Compute,
        };

        static std::vector<std::vector<uint8_t>> compileToSpirvs(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        );

        static std::vector<std::vector<uint8_t>> compileToWgsls(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        );

        // 只做 session 搭建 + loadModuleFromSourceString,不做入口点编译,
        // 返回这次解析实际展开到的所有文件路径(包括 #include/import 递归依赖),用于给缓存 key 提供依赖哈希
        static std::vector<std::string> getDependencyFiles(
            const char* name,
            const char* source,
            const char* fileName,
            std::span<const ShaderMacro> macros,
            bool forWgsl
        );
    };
}
