#pragma once
#include "LocalLightProxy.h"

#include <RenderBase/MaterialProxy.h>
#include <RenderBase/GeometryProxy.h>

namespace hyp
{
    struct DrawItem
    {
        MaterialProxy* material = nullptr;
        GeometryProxy* geometry = nullptr;
        uint32_t sectionIndex = 0;
        // 写进 sIndices 的值: 网格类绘制项是 meshInstanceIndex, Quad 绘制项是 Quad 实例下标
        uint32_t instanceIndex = 0;
        // 以下两项只在 geometry 为空(没有顶点缓冲和索引缓冲, 顶点属性全靠顶点拉取)时有效
        VertexLayout vertexLayout;
        uint32_t vertexCount = 0;

        VertexLayout getVertexLayout() const noexcept
        {
            return geometry ? geometry->getVertexLayout() : vertexLayout;
        }

        uint32_t getGeometryId() const noexcept
        {
            return geometry ? geometry->getId() : 0;
        }

        // 两个 DrawItem 能不能合并成同一次实例化绘制
        bool hasSameGeometry(const DrawItem& other) const noexcept
        {
            if (geometry != other.geometry)
                return false;
            return geometry || (vertexLayout == other.vertexLayout && vertexCount == other.vertexCount);
        }

        void bind(rhi::CommandList& cmdList) const noexcept
        {
            if (geometry)
                geometry->bind(cmdList);
        }

        void draw(rhi::CommandList& cmdList, uint32_t instanceCount, uint32_t firstInstance) const noexcept
        {
            if (geometry)
                geometry->draw(cmdList, sectionIndex, instanceCount, firstInstance);
            else
                cmdList.draw(vertexCount, instanceCount, 0, firstInstance);
        }
    };

    struct DrawItemEntry
    {
        uint64_t sortKey;
        uint32_t itemIndex;

        bool operator<(const DrawItemEntry& other) const
        {
            if (sortKey != other.sortKey)
                return sortKey < other.sortKey;
            return itemIndex < other.itemIndex;
        }
    };

    using DrawItemList = std::vector<DrawItem>;
    using DrawItemBucket = std::vector<DrawItemEntry>;

    struct DrawBatch
    {
        uint32_t itemIndex;
        uint32_t instanceCount;
        uint32_t firstInstance;
    };

    struct DrawBatchList
    {
        std::vector<DrawBatch> batches;
        std::vector<uint32_t> instanceIndices;
    };

    struct RenderData
    {
        std::shared_ptr<DrawItemList> drawItems;
        std::shared_ptr<DrawItemBucket> preDepthBucket;
        std::shared_ptr<DrawItemBucket> opaqueBucket;
        std::shared_ptr<DrawItemBucket> translucentBucket;
        std::shared_ptr<DrawItemBucket> shadowDepthBucket;
        std::shared_ptr<DrawBatchList> opaqueBatches;
        std::shared_ptr<DrawBatchList> shadowDepthBatches;
        std::shared_ptr<std::vector<LocalLightProxy*>> localLights;

        RenderData()
        {
            opaqueBatches = std::make_shared<DrawBatchList>();
            shadowDepthBatches = std::make_shared<DrawBatchList>();
            drawItems = std::make_shared<DrawItemList>();
            preDepthBucket = std::make_shared<DrawItemBucket>();
            opaqueBucket = std::make_shared<DrawItemBucket>();
            translucentBucket = std::make_shared<DrawItemBucket>();
            shadowDepthBucket = std::make_shared<DrawItemBucket>();
            localLights = std::make_shared<std::vector<LocalLightProxy*>>();
        }
    };
}
