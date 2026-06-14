#pragma once
#include <Core/Common/HString.h>

namespace hyp
{
    struct BuiltinRenderTags
    {
        static const HString Opaque;
        static const HString Translucent;
        static const HString ShadowDepth;
        static const HString DepthOnly;
    };

    class RenderTagRegistry
    {
    public:
        RenderTagRegistry();

        uint32_t registerTag(HString renderTag, const std::string& shaderFileName, const std::string& shaderTemplate);

        uint32_t getRenderMask(HString renderTag);

        const std::string& getShaderFileName(HString renderTag);

        const std::string& getShaderTemplate(HString renderTag);

        uint32_t getRenderTagCount() const noexcept
        {
            return mRenderTagCount;
        }

        uint32_t getBuiltinOpaqueMask() const noexcept
        {
            return mBuiltinOpaqueMask;
        }

        uint32_t getBuiltinTranslucentMask() const noexcept
        {
            return mBuiltinTranslucentMask;
        }

        uint32_t getBuiltinShadowDepthMask() const noexcept
        {
            return mBuiltinShadowDepthMask;
        }

        uint32_t getBuiltinDepthOnlyMask() const noexcept
        {
            return mBuiltinDepthOnlyMask;
        }

    private:
        uint32_t mRenderTagCount = 0;
        struct Entry
        {
            uint32_t renderMask;
            std::string shaderFileName;
            std::string shaderTemplate;
        };
        std::unordered_map<HString, Entry> mRenderTagMap;

        uint32_t mBuiltinOpaqueMask;
        uint32_t mBuiltinTranslucentMask;
        uint32_t mBuiltinShadowDepthMask;
        uint32_t mBuiltinDepthOnlyMask;
    };
}
