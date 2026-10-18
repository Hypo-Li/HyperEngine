#pragma once
#include "SurfaceCacheAtlas.h"

#include <Core/Container/IndexedMinHeap.h>
#include <Core/Container/RangeAllocator.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace hyp
{
    class MeshRenderProxy;

    // 登记 Surface Cache 里每个驻留的 card page, 决定哪些页需要重新捕获:
    // 手动失效的页按失效先后优先, 其余按最久没捕获过的顺序轮流刷新
    class SurfaceCacheRecaptureQueue
    {
    public:
        struct PageRef
        {
            MeshRenderProxy* proxy;
            uint32_t cardIndex;
            uint32_t pageLinearIndex;
        };

        SurfaceCacheRecaptureQueue();

        // 为一张卡的 pageCount 个页分配一块连续的记录 id, 返回起始 id
        std::optional<uint32_t> addCard(MeshRenderProxy* proxy, uint32_t cardIndex, uint32_t pageCount, uint32_t frame);

        void removeCard(uint32_t recordBase, uint32_t pageCount) noexcept;

        void invalidateCard(uint32_t recordBase, uint32_t pageCount);

        const PageRef& getPage(uint32_t recordId) const noexcept
        {
            return mRecords[recordId];
        }

        std::optional<uint32_t> peekInvalidated();

        std::optional<uint32_t> peekOldest() const noexcept;

        uint32_t getLastCapturedFrame(uint32_t recordId) const noexcept
        {
            return mLastCapturedFrames.getKey(recordId);
        }

        void markCaptured(uint32_t recordId, uint32_t frame) noexcept;

    private:
        RangeAllocator mRecordAllocator;
        std::vector<PageRef> mRecords;
        std::vector<uint8_t> mInvalidated;
        IndexedMinHeap mLastCapturedFrames;
        std::deque<uint32_t> mInvalidatedQueue;
    };
}
