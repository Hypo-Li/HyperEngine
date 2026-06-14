#pragma once
#include "Instance.h"
#include "PhysicalDevice.h"

namespace hyp::vk
{
    class Queue;
    class CommandPool;
    class MemoryAllocator;
    class DummyRenderPassCache;
    class DescriptorAllocator;
    class SamplerCache;

    class Device : public Refable
    {
    public:
        Device(
            const PhysicalDevice& physicalDevice,
            std::span<const char*> extensions,
            const VkPhysicalDeviceFeatures& features
        );
        ~Device();

        VkDevice getHandle() const noexcept
        {
            return mDevice;
        }

        const PhysicalDevice& getPhysicalDevice() const noexcept
        {
            return mPhysicalDevice;
        }

        Queue& getGraphicsQueue() const noexcept
        {
            return *mGraphicsQueue;
        }

        Queue& getComputeQueue() const noexcept
        {
            return *mComputeQueue;
        }

        Queue& getTransferQueue() const noexcept
        {
            return *mTransferQueue;
        }

        void setupPresentQueue(VkSurfaceKHR surface);

        Queue& getPresentQueue() const noexcept
        {
            return *mPresentQueue;
        }

        CommandPool& getGraphicsCommandPool() const noexcept
        {
            return *mGraphicsCommandPool;
        }

        CommandPool& getComputeCommandPool() const noexcept
        {
            return *mComputeCommandPool;
        }

        CommandPool& getTransferCommandPool() const noexcept
        {
            return *mTransferCommandPool;
        }

        MemoryAllocator& getMemoryAllocator() const noexcept
        {
            return *mMemoryAllocator;
        }

        DescriptorAllocator& getDescriptorAllocator() const noexcept
        {
            return *mDescriptorAllocator;
        }

        DummyRenderPassCache& getDummyRenderPassCache() const noexcept
        {
            return *mDummyRenderPassCache;
        }

        SamplerCache& getSamplerCache() const noexcept
        {
            return *mSamplerCache;
        }

        void waitIdle() const noexcept
        {
            vkDeviceWaitIdle(mDevice);
        }

    private:
        StructureChain createFeatureChain();

    private:
        const PhysicalDevice& mPhysicalDevice;
        VkDevice mDevice{ VK_NULL_HANDLE };
        TRef<Queue> mGraphicsQueue{ nullptr };
        TRef<CommandPool> mGraphicsCommandPool{ nullptr };
        TRef<Queue> mComputeQueue{ nullptr };
        TRef<CommandPool> mComputeCommandPool{ nullptr };
        TRef<Queue> mTransferQueue{ nullptr };
        TRef<CommandPool> mTransferCommandPool{ nullptr };
        TRef<Queue> mPresentQueue{ nullptr };

        std::unique_ptr<MemoryAllocator> mMemoryAllocator;
        std::unique_ptr<DescriptorAllocator> mDescriptorAllocator;
        std::unique_ptr<DummyRenderPassCache> mDummyRenderPassCache;
        std::unique_ptr<SamplerCache> mSamplerCache;
    };
}
