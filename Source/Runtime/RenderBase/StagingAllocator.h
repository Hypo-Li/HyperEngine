#pragma once
#include <optional>
#include <deque>

#include "Vulkan/Buffer.h"

namespace hyp
{
    struct StagingAllocation
    {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
        void* mappedData;
    };

    class StagingAllocator
    {
    public:
        using Allocation = StagingAllocation;

        StagingAllocator(const vk::Device& device, VkDeviceSize ringBufferSize);

        ~StagingAllocator();

        inline size_t getRingBufferSize() const noexcept
        {
            return mRingBuffer->getSize();
        }

        std::optional<Allocation> tryAllocateFromRingBuffer(VkDeviceSize size, VkDeviceSize alignment = 16);

        Allocation allocateSeparateBuffer(VkDeviceSize size);

        std::optional<Allocation> tryAllocate(VkDeviceSize size, VkDeviceSize alignment = 16);

        Allocation forceAllocate(VkDeviceSize size, VkDeviceSize alignment = 16);

        // 提交 command buffer 后, 调用该方法记录已分配内存的释放时间
        void commitAllocations(uint64_t submittedTimelineValue);

        // 每帧开始前调用该方法, 确保 Staging Manager 有尽可能多的可用空间
        void reclaimAllocation(uint64_t timelineValue) noexcept;

    private:
        inline static VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }

    private:
        size_t mHead{ 0 };
        size_t mTail{ 0 };
        TRef<vk::Buffer> mRingBuffer;
        void* mRingBufferMappedData{ nullptr };

        struct RingBlockRecord
        {
            VkDeviceSize size;
            uint64_t signalValue = ~0ull;
        };
        std::vector<RingBlockRecord> mCurrentFrameRingBlocks;
        std::deque<RingBlockRecord> mInFlightRingBlocks;

        struct SeparateBlockRecord
        {
            TRef<vk::Buffer> buffer;
            uint64_t signalValue = ~0ull;
        };
        std::vector<SeparateBlockRecord> mCurrentFrameSeparateBlocks;
        std::deque<SeparateBlockRecord> mInFlightSeparateBlocks;

    };
}
