#pragma once
#include <Core/Common/HString.h>
#include <Core/Common/EnumBitwise.h>

#include "MaterialTypes.h"

namespace hyp
{
    struct BuiltinRenderTags
    {
        static const HString Opaque;
        static const HString Translucent;
        static const HString ShadowDepth;
        static const HString DepthOnly;
    };

    // render tag 的可选特性标记,注册时声明,后续新特性直接在这里加新位,不再需要给
    // registerTag 加新的函数参数
    enum class RenderTagFlags : uint32_t
    {
        None = 0,
        // 该 render tag 的模板对 Opaque 材质完全不依赖材质参数/颜色(只依赖几何形变,即是否有
        // 顶点位置偏移)——约定:声明这个标记的模板的 FragmentMain 不能调用 evaluateMaterial()。
        // 静态、不透明、无顶点位置偏移的材质在这类 render tag 下会被 MaterialProxy 自动重定向到
        // 一份跨材质共享的规范化 shader/pipeline,省去逐材质切换 pipeline 的开销
        Geometry_Only = 1u << 0,
    };
    ENABLE_ENUM_CLASS_BITWISE(RenderTagFlags)

    // 纯配置数据(tag 名字/shader 模板/mask),不依赖 Device,逻辑线程和渲染线程都能安全访问,
    // 由 RenderBackend 持有,通过 Context::getRenderTagRegistry() 暴露给逻辑线程,不做成独立单例
    class RenderTagRegistry
    {
    public:
        RenderTagRegistry();

        uint32_t registerTag(
            HString renderTag,
            const std::string& shaderFileName,
            const std::string& shaderTemplate,
            RenderTagFlags flags = RenderTagFlags::None
        );

        bool hasFlags(HString renderTag, RenderTagFlags flags) const noexcept;

        uint32_t getRenderMask(HString renderTag);

        // Domain+BlendMode -> 适用的内置 renderTag mask,是"这个材质该编译哪些变体"的唯一权威判断,
        // 渲染线程(MaterialProxy)和逻辑线程(MaterialTemplate)都必须用这一份,不要各写一份过滤逻辑
        uint32_t getApplicableRenderMask(MaterialDomain domain, BlendMode blendMode) const noexcept;

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
            RenderTagFlags flags;
        };
        std::unordered_map<HString, Entry> mRenderTagMap;

        uint32_t mBuiltinOpaqueMask;
        uint32_t mBuiltinTranslucentMask;
        uint32_t mBuiltinShadowDepthMask;
        uint32_t mBuiltinDepthOnlyMask;
    };
}
