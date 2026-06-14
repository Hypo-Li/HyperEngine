#include "Queue.h"

namespace hyp::vk
{
    Queue::Queue(const Device& device, uint32_t familyIndex)
        : mDevice(device), mFamilyIndex(familyIndex)
    {
        vkGetDeviceQueue(device.getHandle(), familyIndex, 0, &mQueue);
        mProperties = device.getPhysicalDevice().getQueueFamilyProperties().at(familyIndex);
    }

    VkResult Queue::submit(
        CommandBuffer& commandBuffer,
        std::span<const VkSemaphore> waitSemaphores,
        std::span<const VkPipelineStageFlags> waitStages,
        std::span<const VkSemaphore> signalSemaphores
    )
    {
        assert(commandBuffer.getState() == CommandBuffer::State::Executable);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffers[] = { commandBuffer.getHandle() };
        submitInfo.pCommandBuffers = commandBuffers;
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        auto& signalFence = commandBuffer.getSignalFence();
        if (signalFence.getStatus() == VK_SUCCESS)
        {
            signalFence.reset();
        }

        VkResult result = vkQueueSubmit(mQueue, 1, &submitInfo, signalFence.getHandle());
        commandBuffer.onSubmit();
        return result;
    }

    VkResult Queue::submit(
        CommandBuffer& commandBuffer,
        std::span<const VkSemaphore> waitSemaphores,
        std::span<const VkPipelineStageFlags> waitStages,
        std::span<const uint64_t> waitValues,
        std::span<const VkSemaphore> signalSemaphores,
        std::span<const uint64_t> signalValues
    )
    {
        assert(commandBuffer.getState() == CommandBuffer::State::Executable);

        VkTimelineSemaphoreSubmitInfo timelineInfo = {};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = &timelineInfo;
        submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        VkCommandBuffer commandBuffers[] = { commandBuffer.getHandle() };
        submitInfo.pCommandBuffers = commandBuffers;
        submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        auto& signalFence = commandBuffer.getSignalFence();
        if (signalFence.getStatus() == VK_SUCCESS)
        {
            signalFence.reset();
        }

        VkResult result = vkQueueSubmit(mQueue, 1, &submitInfo, signalFence.getHandle());
        commandBuffer.onSubmit();
        return result;
    }

    VkResult Queue::present(std::span<const VkSemaphore> waitSemaphores, VkSwapchainKHR swapchain, uint32_t imageIndex)
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
        presentInfo.pWaitSemaphores = waitSemaphores.data();
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        presentInfo.pResults = nullptr;

        return vkQueuePresentKHR(mQueue, &presentInfo);
    }

    void Queue::waitIdle() const
    {
        vkQueueWaitIdle(mQueue);
    }
}
