#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <span>

#include "Shader.h"

namespace hyp
{
    // 材质 shader 编译能力的抽象接口——具体实现(基于 Slang)住在 Source/Editor/ShaderCompiler,
    // 编辑期专属工具库,RenderBase 作为更底层的库不能反过来依赖它, 只能通过 ModuleServiceRegistry
    // (见 Core/Common/ModuleServiceRegistry.h)按接口类型查找已注册的实现。运行期(未构建/未链接
    // Editor 工具链的配置)不存在任何实现, ModuleServiceRegistry::getService<IShaderCompiler>()
    // 返回 nullptr, ShaderCache 据此走只读查找烘焙数据的路径, 不再具备编译能力。
    class IShaderCompiler
    {
    public:
        enum class ShaderStage
        {
            Vertex,
            Fragment,
            Compute,
        };

        virtual ~IShaderCompiler() = default;

        virtual std::vector<std::vector<uint8_t>> compileToSpirvs(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            const rhi::ShaderCompileTarget& target,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        ) = 0;

        virtual std::vector<std::vector<uint8_t>> compileToWgsls(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            const rhi::ShaderCompileTarget& target,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        ) = 0;

        // 返回这次解析实际展开到的所有文件路径(包括 #include/import 递归依赖),用于给缓存 key 提供依赖哈希
        virtual std::vector<std::string> getDependencyFiles(
            const char* name,
            const char* source,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            std::span<const ShaderMacro> macros,
            const rhi::ShaderCompileTarget& target
        ) = 0;
    };
}
