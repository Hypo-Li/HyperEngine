#include "StagingAllocator.h"

namespace hyp
{
    StagingAllocator::StagingAllocator(const vk::Device& device, VkDeviceSize ringBufferSize)
    {
        vk::Buffer::CreateInfo bufferInfo;
        bufferInfo.size = ringBufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        mRingBuffer = new vk::Buffer(device, bufferInfo, VMA_MEMORY_USAGE_CPU_ONLY, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        mRingBufferMappedData = mRingBuffer->getMemory()->map();
    }

    StagingAllocator::~StagingAllocator()
    {
        mRingBuffer->getMemory()->unmap();
    }

    std::optional<StagingAllocation> StagingAllocator::tryAllocateFromRingBuffer(VkDeviceSize size, VkDeviceSize alignment)
    {
        // 逻辑上的 Head 对齐
        VkDeviceSize alignedHead = alignUp(mHead, alignment);
        VkDeviceSize bufferSize = mRingBuffer->getSize();

        // 关键逻辑：如果当前分配导致 Head 超过 Tail 超过一圈的空间，说明满了
        if (alignedHead + size > mTail + bufferSize)
            return std::nullopt; // 空间不足

        // 处理回绕时的连续性：Vulkan 的 copyBuffer 要求 Source Offset 在单次拷贝中不能越界
        // 如果当前分段在物理 Buffer 尾部放不下，我们需要跳过这段空间（Padding）
        VkDeviceSize physicalOffset = alignedHead % bufferSize;
        if (physicalOffset + size > bufferSize)
        {
            // 跳过末尾碎片的 Padding, 直接从物理 0 开始分配
            VkDeviceSize padding = bufferSize - physicalOffset;
            alignedHead = alignUp(alignedHead + padding, alignment);

            // 再次检查空间
            if (alignedHead + size > mTail + bufferSize)
            {
                return std::nullopt;
            }
            physicalOffset = 0;
        }

        mCurrentFrameRingBlocks.emplace_back(alignedHead + size - mHead);
        mHead = alignedHead + size;

        Allocation allocation;
        allocation.buffer = mRingBuffer->getHandle();
        allocation.offset = physicalOffset;
        allocation.size = size;
        allocation.mappedData = static_cast<uint8_t*>(mRingBufferMappedData) + physicalOffset;
        return allocation;
    }

    StagingAllocation StagingAllocator::allocateSeparateBuffer(VkDeviceSize size)
    {
        vk::Buffer::CreateInfo bufferInfo;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        TRef<vk::Buffer> separateBuffer = new vk::Buffer(mRingBuffer->getDevice(), bufferInfo, VMA_MEMORY_USAGE_CPU_ONLY, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        mCurrentFrameSeparateBlocks.emplace_back(separateBuffer);

        Allocation allocation;
        allocation.buffer = separateBuffer->getHandle();
        allocation.offset = 0;
        allocation.size = size;
        allocation.mappedData = separateBuffer->getMemory()->map();
        return allocation;
    }

    std::optional<StagingAllocation> StagingAllocator::tryAllocate(VkDeviceSize size, VkDeviceSize alignment)
    {
        // 对于大于 RingBuffer 的请求, 直接分配单独缓冲
        if (size > mRingBuffer->getSize())
        {
            return allocateSeparateBuffer(size);
        }

        auto allocation = tryAllocateFromRingBuffer(size, alignment);
        // 对于大于 1/2 RingBuffer, 且尝试分配失败时,
        // 我们认为它很难有机会从 RingBuffer 中分配, 也分配单独缓冲
        if (!allocation && size >= (mRingBuffer->getSize() / 2))
        {
            return allocateSeparateBuffer(size);
        }
        return allocation;
    }

    StagingAllocation StagingAllocator::forceAllocate(VkDeviceSize size, VkDeviceSize alignment)
    {
        auto allocation = tryAllocate(size, alignment);
        if (allocation)
        {
            return *allocation;
        }
        return allocateSeparateBuffer(size);
    }

    void StagingAllocator::commitAllocations(uint64_t submittedTimelineValue)
    {
        for (auto& block : mCurrentFrameRingBlocks)
        {
            block.signalValue = submittedTimelineValue;
            mInFlightRingBlocks.push_back(block);
        }
        mCurrentFrameRingBlocks.clear();

        for (auto& block : mCurrentFrameSeparateBlocks)
        {
            block.signalValue = submittedTimelineValue;
            mInFlightSeparateBlocks.push_back(block);
        }
        mCurrentFrameSeparateBlocks.clear();
    }

    void StagingAllocator::reclaimAllocation(uint64_t timelineValue) noexcept
    {
        while (!mInFlightRingBlocks.empty() && mInFlightRingBlocks.front().signalValue <= timelineValue)
        {
            mTail += mInFlightRingBlocks.front().size;
            mInFlightRingBlocks.pop_front();
        }

        while (!mInFlightSeparateBlocks.empty() && mInFlightSeparateBlocks.front().signalValue <= timelineValue)
        {
            mInFlightSeparateBlocks.front().buffer->getMemory()->unmap();
            mInFlightSeparateBlocks.pop_front();
        }
    }

}
