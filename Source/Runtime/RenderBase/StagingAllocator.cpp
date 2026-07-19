#include "StagingAllocator.h"

namespace hyp
{
    StagingAllocator::StagingAllocator(const rhi::Device& device, uint32_t pageSize, uint32_t maxPageCount) :
        mDevice(device),
        mPageSize(pageSize),
        mMaxPageCount(maxPageCount)
    {
        mPages.reserve(maxPageCount);
    }

    StagingAllocator::~StagingAllocator()
    {
        for (auto& page : mPages)
        {
            if (page.mappedData)
            {
                page.buffer->unmap();
                page.mappedData = nullptr;
            }
        }
    }

    std::optional<StagingAllocation> StagingAllocator::tryAllocate(size_t size, uint32_t alignment)
    {
        if (size > mPageSize)
            return allocateTransient(size);

        auto allocation = tryAllocateFromPages(size, alignment);
        // 对于大于 1/2 PageSize, 且尝试分配失败时,
        // 我们认为它很难有机会从 Page 中分配, 也分配单独缓冲
        if (!allocation && size >= mPageSize / 2)
        {
            return allocateTransient(size);
        }
        return allocation;
    }

    StagingAllocation StagingAllocator::forceAllocate(size_t size, uint32_t alignment)
    {
        auto allocation = tryAllocateFromPages(size, alignment);
        if (allocation)
        {
            return *allocation;
        }
        return allocateTransient(size);
    }

    void StagingAllocator::commitAllocations(uint64_t signalValue)
    {
        for (auto pendingRecord : mPendingPages)
        {
            pendingRecord.signalValue = signalValue;
            auto& page = mPages[pendingRecord.pageIndex];
            page.buffer->unmap();
            page.mappedData = nullptr;
            page.isPending = false;
            mSubmittedPages.push_back(pendingRecord);
        }
        mPendingPages.clear();

        for (auto& pendingRecord : mPendingTransients)
        {
            pendingRecord.buffer->unmap();
            pendingRecord.signalValue = signalValue;
            mSubmittedTransients.push_back(pendingRecord);
        }
        mPendingTransients.clear();
    }

    void StagingAllocator::reclaimAllocations(uint64_t currentValue)
    {
        uint32_t reclaimedPageCount = 0;
        for (auto pageeRecord : mSubmittedPages)
        {
            if (pageeRecord.signalValue > currentValue)
                break;

            auto& page = mPages[pageeRecord.pageIndex];
            page.mappedData = page.buffer->map();
            page.offset = 0;
            reclaimedPageCount++;
        }
        if (reclaimedPageCount > 0)
        {
            mSubmittedPages.erase(
                mSubmittedPages.begin(),
                mSubmittedPages.begin() + reclaimedPageCount
            );
        }

        uint32_t reclaimedTransientCount = 0;
        for (auto transientRecord : mSubmittedTransients)
        {
            if (transientRecord.signalValue > currentValue)
                break;
            reclaimedTransientCount++;
        }
        if (reclaimedTransientCount > 0)
        {
            mSubmittedTransients.erase(
                mSubmittedTransients.begin(),
                mSubmittedTransients.begin() + reclaimedTransientCount
            );
        }
    }

    static uint32_t alignUp(uint32_t value, uint32_t alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    std::optional<StagingAllocation> StagingAllocator::tryAllocateFromPages(size_t size, uint32_t alignment)
    {
        if (size > mPageSize)
            return std::nullopt;

        // 先尝试从当前 Pending Pages 中寻找可用于分配的 Page
        for (auto pending : mPendingPages)
        {
            auto& page = mPages[pending.pageIndex];
            uint32_t alignedOffset = alignUp(page.offset, alignment);
            if (mPageSize - alignedOffset >= size)
            {
                page.offset = alignedOffset + static_cast<uint32_t>(size);
                Allocation allocation;
                allocation.buffer = page.buffer;
                allocation.offset = alignedOffset;
                allocation.size = size;
                allocation.mappedData = static_cast<uint8_t*>(page.mappedData) + alignedOffset;
                return allocation;
            }
        }

        // 如果 Pending Pages 中没有可用的 Page, 则尝试从 Pages 中寻找一个有足够size 且 mappedData 有效的 Page
        for (uint32_t i = 0; i < mPages.size(); ++i)
        {
            auto& page = mPages[i];
            if (page.mappedData && !page.isPending)
            {
                page.isPending = true;
                mPendingPages.emplace_back(i);
                page.offset = static_cast<uint32_t>(size);
                Allocation allocation;
                allocation.buffer = page.buffer;
                allocation.offset = 0;
                allocation.size = size;
                allocation.mappedData = page.mappedData;
                return allocation;
            }
        }

        // 如果所有现存 Pages 都没有可用空间, 则在不超过 maxPageCount的范围内创建新的 Page
        if (mPages.size() < mMaxPageCount)
        {
            uint32_t pageIndex = static_cast<uint32_t>(mPages.size());
            rhi::BufferDesc bufferDesc;
            bufferDesc.size = mPageSize;
            bufferDesc.usage = rhi::BufferUsage::Transfer_Src;
            TRef<rhi::Buffer> buffer = mDevice.createBuffer(bufferDesc, rhi::MemoryType::Cpu_To_Gpu);
            std::string debugName = "StagingPage" + std::to_string(pageIndex);
            buffer->setDebugName(std::string_view(debugName));
            auto& page = mPages.emplace_back(buffer, static_cast<uint32_t>(size), true, buffer->map());
            mPendingPages.emplace_back(pageIndex);

            Allocation allocation;
            allocation.buffer = buffer.get();
            allocation.offset = 0;
            allocation.size = size;
            allocation.mappedData = page.mappedData;
            return allocation;
        }

        return std::nullopt;
    }

    StagingAllocation StagingAllocator::allocateTransient(size_t size)
    {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = size;
        bufferDesc.usage = rhi::BufferUsage::Transfer_Src;
        TRef<rhi::Buffer> transientBuffer = mDevice.createBuffer(bufferDesc, rhi::MemoryType::Cpu_To_Gpu);
        transientBuffer->setDebugName("StagingTransient");
        mPendingTransients.emplace_back(transientBuffer);

        Allocation allocation;
        allocation.buffer = transientBuffer.get();
        allocation.offset = 0;
        allocation.size = size;
        allocation.mappedData = transientBuffer->map();
        return allocation;
    }
}
