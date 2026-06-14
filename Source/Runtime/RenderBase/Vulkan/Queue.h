#pragma once
#include "Device.h"
#include "CommandBuffer.h"

namespace hyp::vk
{
    class Queue : public Refable
    {
    public:
        Queue(const Device& device, uint32_t familyIndex);
        ~Queue() = default;

        VkQueue getHandle() const
        {
            return mQueue;
        }

        inline uint32_t getFamilyIndex() const
        {
            return mFamilyIndex;
        }

        const VkQueueFamilyProperties& getProperties() const
        {
            return mProperties;
        }

        VkResult submit(
            CommandBuffer& commandBuffer,
            std::span<const VkSemaphore> waitSemaphores,
            std::span<const VkPipelineStageFlags> waitStages,
            std::span<const VkSemaphore> signalSemaphores
        );

        VkResult submit(
            CommandBuffer& commandBuffer,
            std::span<const VkSemaphore> waitSemaphores,
            std::span<const VkPipelineStageFlags> waitStages,
            std::span<const uint64_t> waitValues,
            std::span<const VkSemaphore> signalSemaphores,
            std::span<const uint64_t> signalValues
        );

        VkResult present(std::span<const VkSemaphore> waitSemaphores, VkSwapchainKHR swapchain, uint32_t imageIndex);

        void waitIdle() const;

    private:
        const Device& mDevice;
        VkQueue mQueue{ VK_NULL_HANDLE };
        uint32_t mFamilyIndex;
        VkQueueFamilyProperties mProperties;
    };
}
