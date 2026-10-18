#pragma once
#include "DrawableProxy.h"
#include "QuadManager.h"

#include <Core/Math/Vector.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hyp
{
    class MaterialProxy;
    class QuadManager;

    enum class QuadBillboardMode : uint8_t
    {
        None,
        Screen,
        Axis
    };

    // QuadInstanceData 里不由 QuadRenderProxy 自己维护(下标、元素区间)之外的全部字段
    struct QuadPlacement
    {
        QuadBillboardMode billboardMode = QuadBillboardMode::None;
        float2 blockSize = float2(0.0f);
        float2 anchor = float2(0.0f);
        float depthBias = 0.0f;
        float autoScaleStrength = 0.0f;
        float referenceDistance = 1.0f;
        float minScale = 0.0f;
        float maxScale = 1.0e6f;
        uint32_t tint = 0xFFFFFFFFu;
    };

    class QuadRenderProxy : public DrawableProxy
    {
    public:
        struct Desc
        {
            DrawableProxy::Desc drawable;
            MaterialProxy* material = nullptr;
            std::vector<QuadElement> elements;
            QuadPlacement placement;
        };

        QuadRenderProxy(SceneProxy& sceneProxy, Desc desc);

        ~QuadRenderProxy() noexcept override;

        void setMaterial(MaterialProxy* material) noexcept;

        void setElements(std::vector<QuadElement>&& elements);

        void setPlacement(const QuadPlacement& placement);

        std::span<const DrawItem> getDrawItems(const SceneView& view) const noexcept override
        {
            return mDrawItems;
        }

    private:
        void uploadElements(std::vector<QuadElement>&& elements);

        void uploadInstanceData();

        void syncBillboardFlags() noexcept;

        void releaseElementRange() noexcept;

        void refreshDrawItems();

    private:
        QuadManager& mQuadManager;
        MaterialProxy* mMaterial = nullptr;
        QuadPlacement mPlacement;

        std::optional<uint32_t> mQuadInstanceIndex;
        std::optional<uint32_t> mElementBase;
        uint32_t mElementRangeSize = 0;

        std::vector<DrawItem> mDrawItems;
    };

    template <>
    struct TProxyTraits<ProxyType::Quad_Render>
    {
        using Proxy = QuadRenderProxy;
    };
}
