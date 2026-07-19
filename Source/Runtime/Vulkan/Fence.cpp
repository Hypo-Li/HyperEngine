#include "Fence.h"

namespace hyp::vk
{
    Fence::Fence(const Device& device) : rhi::Fence(device)
    {
        VkSemaphoreTypeCreateInfo timelineInfo = {};
        timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timelineInfo.initialValue = 0;

        VkSemaphoreCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        createInfo.pNext = &timelineInfo;

        vkCreateSemaphore(device.getHandle(), &createInfo, nullptr, &mSemaphore);
    }

    Fence::~Fence() noexcept
    {
        vkDestroySemaphore(getVkDevice().getHandle(), mSemaphore, nullptr);
    }

    uint64_t Fence::getCompletedValue() const
    {
        uint64_t value = 0;
        VkResult result = vkGetSemaphoreCounterValue(getVkDevice().getHandle(), mSemaphore, &value);
        assert(result == VK_SUCCESS);
        return value;
    }

    void Fence::wait(uint64_t value, uint64_t timeout)
    {
        VkSemaphoreWaitInfo waitInfo = {};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &mSemaphore;
        waitInfo.pValues = &value;

        vkWaitSemaphores(getVkDevice().getHandle(), &waitInfo, timeout);
    }
}
