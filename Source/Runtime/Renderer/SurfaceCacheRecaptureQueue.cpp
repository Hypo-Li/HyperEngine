#include "SurfaceCacheRecaptureQueue.h"

namespace hyp
{
    SurfaceCacheRecaptureQueue::SurfaceCacheRecaptureQueue() :
        mRecordAllocator(kSurfaceCacheAtlasMaxCardPages),
        mRecords(kSurfaceCacheAtlasMaxCardPages, PageRef{ nullptr, 0, 0 }),
        mInvalidated(kSurfaceCacheAtlasMaxCardPages, 0u),
        mLastCapturedFrames(kSurfaceCacheAtlasMaxCardPages)
    {
    }

    std::optional<uint32_t> SurfaceCacheRecaptureQueue::addCard(MeshRenderProxy* proxy, uint32_t cardIndex, uint32_t pageCount, uint32_t frame)
    {
        std::optional<uint32_t> recordBase = mRecordAllocator.allocate(pageCount);
        if (!recordBase)
            return std::nullopt;

        for (uint32_t pageLinearIndex = 0; pageLinearIndex < pageCount; ++pageLinearIndex)
        {
            const uint32_t recordId = *recordBase + pageLinearIndex;
            mRecords[recordId] = PageRef{ proxy, cardIndex, pageLinearIndex };
            mInvalidated[recordId] = 0u;
            mLastCapturedFrames.insert(recordId, frame);
        }
        return recordBase;
    }

    void SurfaceCacheRecaptureQueue::removeCard(uint32_t recordBase, uint32_t pageCount) noexcept
    {
        for (uint32_t recordId = recordBase; recordId < recordBase + pageCount; ++recordId)
        {
            mLastCapturedFrames.remove(recordId);
            mRecords[recordId] = PageRef{ nullptr, 0, 0 };
            mInvalidated[recordId] = 0u;
        }
        mRecordAllocator.free(recordBase, pageCount);
    }

    void SurfaceCacheRecaptureQueue::invalidateCard(uint32_t recordBase, uint32_t pageCount)
    {
        for (uint32_t recordId = recordBase; recordId < recordBase + pageCount; ++recordId)
        {
            if (mInvalidated[recordId])
                continue;
            mInvalidated[recordId] = 1u;
            mInvalidatedQueue.push_back(recordId);
        }
    }

    std::optional<uint32_t> SurfaceCacheRecaptureQueue::peekInvalidated()
    {
        // 队列里可能留着已经被捕获或已经移除的记录, 以 mInvalidated 为准
        while (!mInvalidatedQueue.empty())
        {
            const uint32_t recordId = mInvalidatedQueue.front();
            if (mInvalidated[recordId])
                return recordId;
            mInvalidatedQueue.pop_front();
        }
        return std::nullopt;
    }

    std::optional<uint32_t> SurfaceCacheRecaptureQueue::peekOldest() const noexcept
    {
        if (mLastCapturedFrames.empty())
            return std::nullopt;
        return mLastCapturedFrames.top();
    }

    void SurfaceCacheRecaptureQueue::markCaptured(uint32_t recordId, uint32_t frame) noexcept
    {
        mLastCapturedFrames.update(recordId, frame);
        mInvalidated[recordId] = 0u;
    }
}
