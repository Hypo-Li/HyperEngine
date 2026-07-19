#pragma once
#include "RenderData.h"
#include "SceneView.h"
#include "RenderableProxy.h"

#include <Core/Math/Sphere.h>
#include <Core/Math/Box.h>

namespace hyp
{
    class MeshRenderProxy : public RenderableProxy
    {
    public:
        MeshRenderProxy();

        ~MeshRenderProxy() noexcept = default;

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

        void setBoundingBox(Boxd boundingBox) noexcept
        {
            mBoundingBox = boundingBox;
            double3 boxSize = mBoundingBox.getSize();
            double radius = Math::length(boxSize) * 0.5;
            mBoundingSphere = Sphered(mBoundingBox.getCenter(), radius);
        }

        const Boxd& getBoundingBox() const noexcept
        {
            return mBoundingBox;
        }

        const Sphered& getBoundingSphere() const noexcept
        {
            return mBoundingSphere;
        }

        struct LOD
        {
            uint32_t startItem;
            uint32_t itemCount;
            float minScreenPercentage;
        };
        void setDrawItems(std::vector<DrawItem>&& drawItems, std::vector<LOD>&& lods) noexcept
        {
            mDrawItems = std::move(drawItems);
            mLODs = std::move(lods);
        }

        std::span<const DrawItem> getDrawItems(const SceneView& view) const noexcept
        {
            if (mLODs.empty())
                return {};

            auto lod = mLODs[0];
            return { &mDrawItems[lod.startItem], lod.itemCount };
        }

    private:
        uint32_t mInstanceIndex;
        uint32_t mLayerMask = ~0u;
        uint32_t mRenderMask = ~0u;
        int8_t mSortOrder = 0;
        Boxd mBoundingBox;
        Sphered mBoundingSphere;
        std::vector<DrawItem> mDrawItems;
        std::vector<LOD> mLODs;
    };

    template <>
    struct TProxyTraits<ProxyType::Mesh_Render>
    {
        using Proxy = MeshRenderProxy;
    };
}
