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
            Geometry,
            TessControl,
            TessEvaluation,
            Compute,
        };

        static std::vector<std::vector<uint32_t>> compileToSpirvs(
            const char* name,
            const char* source,
            std::span<const ShaderStage> stages,
            const char* fileName,
            std::span<const ShaderMacro> macros = {},
            bool optimize = true,
            bool withDebugInfo = false
        );
    };
}
