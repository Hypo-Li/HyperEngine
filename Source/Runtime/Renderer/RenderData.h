#pragma once
#include <RenderBase/MaterialProxy.h>
#include <RenderBase/GeometryProxy.h>

namespace hyp
{
    struct DrawItem
    {
        MaterialProxy* material = nullptr;
        GeometryProxy* geometry = nullptr;
        uint32_t sectionIndex = 0;
        uint32_t instanceIndex = 0;
        uint32_t subInstanceIndex = 0;
        uint32_t subInstanceCount = 0;
    };

    struct SortableDrawItem
    {
        uint64_t sortKey;
        DrawItem drawItem;

        bool operator<(const SortableDrawItem& other) const
        {
            return sortKey < other.sortKey;
        }
    };

    using DrawItemBucket = std::vector<SortableDrawItem>;

    struct RenderData
    {
        std::shared_ptr<DrawItemBucket> preDepthBucket;
        std::shared_ptr<DrawItemBucket> opaqueBucket;
        std::shared_ptr<DrawItemBucket> translucentBucket;
        std::shared_ptr<DrawItemBucket> shadowDepthBucket;

        RenderData()
        {
            preDepthBucket = std::make_shared<DrawItemBucket>();
            opaqueBucket = std::make_shared<DrawItemBucket>();
            translucentBucket = std::make_shared<DrawItemBucket>();
            shadowDepthBucket = std::make_shared<DrawItemBucket>();
        }
    };
}
