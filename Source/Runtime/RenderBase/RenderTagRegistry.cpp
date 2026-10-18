#include "RenderTagRegistry.h"

#include <filesystem>
#include <fstream>

namespace hyp
{
    const HString BuiltinRenderTags::Opaque = "Builtin_Opaque";
    const HString BuiltinRenderTags::Translucent = "Builtin_Translucent";
    const HString BuiltinRenderTags::ShadowDepth = "Builtin_ShadowDepth";
    const HString BuiltinRenderTags::DepthOnly = "Builtin_DepthOnly";
    const HString BuiltinRenderTags::CardCapture = "Builtin_CardCapture";
    const HString BuiltinRenderTags::SkyBackground = "Builtin_SkyBackground";

    std::string readShaderTemplate(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        file.seekg(0, std::ios::end);
        size_t size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string shaderTemplate(size, '\0');
        file.read(shaderTemplate.data(), size);
        return shaderTemplate;
    }

    RenderTagRegistry::RenderTagRegistry(const std::filesystem::path& shaderDir)
    {
        std::string opaquePath = (shaderDir / "Material/Opaque.slang").string();
        std::string translucentPath = (shaderDir / "Material/Translucent.slang").string();
        std::string shadowDepthPath = (shaderDir / "Material/ShadowDepth.slang").string();
        std::string cardCapturePath = (shaderDir / "Material/CardCapture.slang").string();
        std::string skyBackgroundPath = (shaderDir / "Material/SkyBackground.slang").string();

        mBuiltinOpaqueMask = registerTag(BuiltinRenderTags::Opaque, opaquePath, readShaderTemplate(opaquePath));
        mBuiltinTranslucentMask = registerTag(BuiltinRenderTags::Translucent, translucentPath, readShaderTemplate(translucentPath));
        mBuiltinShadowDepthMask = registerTag(BuiltinRenderTags::ShadowDepth, shadowDepthPath, readShaderTemplate(shadowDepthPath), RenderTagFlags::Geometry_Only);
        mBuiltinDepthOnlyMask = registerTag(BuiltinRenderTags::DepthOnly, shadowDepthPath, readShaderTemplate(shadowDepthPath), RenderTagFlags::Geometry_Only);
        mBuiltinCardCaptureMask = registerTag(BuiltinRenderTags::CardCapture, cardCapturePath, readShaderTemplate(cardCapturePath));
        mBuiltinSkyBackgroundMask = registerTag(BuiltinRenderTags::SkyBackground, skyBackgroundPath, readShaderTemplate(skyBackgroundPath));
    }

    uint32_t RenderTagRegistry::registerTag(
        HString renderTag,
        const std::string& shaderFileName,
        const std::string& shaderTemplate,
        RenderTagFlags flags
    )
    {
        auto it = mRenderTagMap.find(renderTag);
        if (it == mRenderTagMap.end())
        {
            auto& entry = mRenderTagMap[renderTag];
            entry.renderMask = 1u << mRenderTagCount;
            entry.shaderFileName = shaderFileName;
            entry.shaderTemplate = shaderTemplate;
            entry.flags = flags;
            mRenderTagCount++;
            return entry.renderMask;
        }
        return it->second.renderMask;
    }

    std::vector<HString> RenderTagRegistry::getRenderTags() const
    {
        std::vector<HString> renderTags;
        renderTags.reserve(mRenderTagMap.size());
        for (const auto& [renderTag, entry] : mRenderTagMap)
            renderTags.push_back(renderTag);
        return renderTags;
    }

    bool RenderTagRegistry::hasFlags(HString renderTag, RenderTagFlags flags) const noexcept
    {
        auto it = mRenderTagMap.find(renderTag);
        if (it == mRenderTagMap.end())
            return false;
        return enumHasAllFlags(it->second.flags, flags);
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

    const std::string& RenderTagRegistry::getShaderFileName(HString renderTag) const
    {
        return mRenderTagMap.at(renderTag).shaderFileName;
    }

    const std::string& RenderTagRegistry::getShaderTemplate(HString renderTag) const
    {
        return mRenderTagMap.at(renderTag).shaderTemplate;
    }

    uint32_t RenderTagRegistry::getBuiltinRenderMask(MaterialDomain domain, BlendMode blendMode) const noexcept
    {
        uint32_t renderMask = 0u;
        switch (domain)
        {
        case MaterialDomain::Surface:
        {
            if (blendMode == BlendMode::Opaque || blendMode == BlendMode::Masked)
            {
                renderMask |= mBuiltinOpaqueMask;
                renderMask |= mBuiltinDepthOnlyMask;
                renderMask |= mBuiltinShadowDepthMask;
                renderMask |= mBuiltinCardCaptureMask;
            }
            else
            {
                renderMask |= mBuiltinTranslucentMask;
            }
            break;
        }
        case MaterialDomain::Sky:
        {
            renderMask |= mBuiltinSkyBackgroundMask;
            break;
        }
        default:
            break;
        }
        return renderMask;
    }

    std::string RenderTagRegistry::makeShaderSource(HString renderTag, const std::string& materialImplementation, VertexLayout vertexLayout) const
    {
        std::string shaderSource = getShaderTemplate(renderTag);

        std::string_view materialImplementationTag = "[MATERIAL_IMPLEMENTATION]";
        auto materialImplementationPos = shaderSource.find(materialImplementationTag);
        if (materialImplementationPos != std::string::npos)
        {
            shaderSource.replace(materialImplementationPos, materialImplementationTag.length(), materialImplementation);
        }

        std::string_view vertexLayoutImplementationTag = "[VERTEX_LAYOUT_IMPLEMENTATION]";
        auto vertexLayoutImplementationPos = shaderSource.find(vertexLayoutImplementationTag);
        if (vertexLayoutImplementationPos != std::string::npos)
        {
            shaderSource.replace(
                vertexLayoutImplementationPos,
                vertexLayoutImplementationTag.length(),
                vertexLayout.getVertexAttributeImplementation()
            );
        }

        return shaderSource;
    }

}
