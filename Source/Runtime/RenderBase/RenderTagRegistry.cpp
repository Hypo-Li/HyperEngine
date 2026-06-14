#include "RenderTagRegistry.h"

#include <filesystem>
#include <fstream>

namespace hyp
{
    const HString BuiltinRenderTags::Opaque = "Builtin_Opaque";
    const HString BuiltinRenderTags::Translucent = "Builtin_Translucent";
    const HString BuiltinRenderTags::ShadowDepth = "Builtin_ShadowDepth";
    const HString BuiltinRenderTags::DepthOnly = "Builtin_DepthOnly";

    std::string readShaderTemplate(const std::filesystem::path& path)
    {
        std::string shaderTemplate;
        std::ifstream file(path);
        shaderTemplate = std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return shaderTemplate;
    }

    RenderTagRegistry::RenderTagRegistry()
    {
        mBuiltinOpaqueMask = registerTag(BuiltinRenderTags::Opaque, SHADER_DIR "/Material/Opaque.slang", readShaderTemplate(SHADER_DIR "/Material/Opaque.slang"));
        mBuiltinTranslucentMask = registerTag(BuiltinRenderTags::Translucent, SHADER_DIR "/Material/Opaque.slang", readShaderTemplate(SHADER_DIR "/Material/Opaque.slang"));
        mBuiltinShadowDepthMask = registerTag(BuiltinRenderTags::ShadowDepth, SHADER_DIR "/Material/ShadowDepth.slang", readShaderTemplate(SHADER_DIR "/Material/ShadowDepth.slang"));
        mBuiltinDepthOnlyMask = registerTag(BuiltinRenderTags::DepthOnly, SHADER_DIR "/Material/ShadowDepth.slang", readShaderTemplate(SHADER_DIR "/Material/ShadowDepth.slang"));
    }

    uint32_t RenderTagRegistry::registerTag(
        HString renderTag,
        const std::string& shaderFileName,
        const std::string& shaderTemplate
    )
    {
        auto it = mRenderTagMap.find(renderTag);
        if (it == mRenderTagMap.end())
        {
            auto& entry = mRenderTagMap[renderTag];
            entry.renderMask = 1u << mRenderTagCount;
            entry.shaderFileName = shaderFileName;
            entry.shaderTemplate = shaderTemplate;
            mRenderTagCount++;
            return entry.renderMask;
        }
        return it->second.renderMask;
    }

    uint32_t RenderTagRegistry::getRenderMask(HString renderTag)
    {
        auto it = mRenderTagMap.find(renderTag);
        if (it != mRenderTagMap.end())
        {
            return it->second.renderMask;
        }
        return 0u;
    }

    const std::string& RenderTagRegistry::getShaderFileName(HString renderTag)
    {
        return mRenderTagMap.at(renderTag).shaderFileName;
    }

    const std::string& RenderTagRegistry::getShaderTemplate(HString renderTag)
    {
        return mRenderTagMap.at(renderTag).shaderTemplate;
    }


}
