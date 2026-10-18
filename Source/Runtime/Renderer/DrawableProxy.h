#pragma once
#include "InstanceProxy.h"
#include "RenderData.h"

#include <Core/Math/Box.h>
#include <Core/Math/Sphere.h>

#include <cstdint>
#include <span>

namespace hyp
{
    class SceneView;

    class DrawableProxy : public InstanceProxy
    {
    public:
        struct Desc
        {
            InstanceProxy::Desc instance;
            Boxd boundingBox;
            uint32_t renderMask = ~0u;
            int8_t sortOrder = 0;
        };

        DrawableProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~DrawableProxy() noexcept override = default;

        void setLayerMask(uint32_t layerMask) noexcept
        {
            mLayerMask = layerMask;
        }

        uint32_t getLayerMask() const noexcept
        {
            return mLayerMask;
        }

        void setRenderMask(uint32_t renderMask) noexcept
        {
            mRenderMask = renderMask;
        }

        uint32_t getRenderMask() const noexcept
        {
            return mRenderMask;
        }

        void setSortOrder(int8_t sortOrder) noexcept
        {
            mSortOrder = sortOrder;
        }

        int8_t getSortOrder() const noexcept
        {
            return mSortOrder;
        }

        virtual void setBoundingBox(Boxd boundingBox) noexcept;

        // 把 InstanceProxy::setMobility 藏起来, 让子类能在 mobility 变化时做额外的事
        virtual void setMobility(Mobility mobility) noexcept;

        const Boxd& getBoundingBox() const noexcept
        {
            return mBoundingBox;
        }

        const Sphered& getBoundingSphere() const noexcept
        {
            return mBoundingSphere;
        }

        virtual std::span<const DrawItem> getDrawItems(const SceneView& view) const noexcept = 0;

    private:
        uint32_t mLayerMask = ~0u;
        uint32_t mRenderMask = ~0u;
        int8_t mSortOrder = 0;
        Boxd mBoundingBox;
        Sphered mBoundingSphere;
    };
}
