#pragma once
#include <RenderBase/ShaderCompiler.h>

namespace hyp
{
    // IShaderCompiler 唯一的具体实现, 基于 Slang——这个类, 以及它唯一依赖的 slang::slang,
    // 只存在于编辑期工具链里, RenderBase 及其运行期消费方从不 #include 这个头文件、
    // 也从不链接这个 target。
    class SlangShaderCompiler : public IShaderCompiler
    {
    public:
        std::vector<std::vector<uint8_t>> compileToSpirvs(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            const rhi::ShaderCompileTarget& target,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        ) override;

        std::vector<std::vector<uint8_t>> compileToWgsls(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            const rhi::ShaderCompileTarget& target,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        ) override;

        std::vector<std::string> getDependencyFiles(
            const char* name,
            const char* source,
            const char* fileName,
            const std::filesystem::path& shaderDir,
            std::span<const ShaderMacro> macros,
            const rhi::ShaderCompileTarget& target
        ) override;
    };
}
