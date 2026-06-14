#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Fence : public Refable
    {
    public:
        Fence(const Device& device, VkFenceCreateFlags flags) noexcept
            : mDevice(device)
        {
            VkFenceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            createInfo.flags = flags;

            vkCreateFence(mDevice.getHandle(), &createInfo, nullptr, &mFence);
        }

        ~Fence() noexcept
        {
            vkDestroyFence(mDevice.getHandle(), mFence, nullptr);
        }

        VkFence getHandle() const noexcept
        {
            return mFence;
        }

        VkResult wait(uint64_t timeout = std::numeric_limits<uint64_t>::max()) const noexcept
        {
            return vkWaitForFences(mDevice.getHandle(), 1, &mFence, VK_TRUE, timeout);
        }

        VkResult reset() const noexcept
        {
            return vkResetFences(mDevice.getHandle(), 1, &mFence);
        }

        VkResult getStatus() const noexcept
        {
            return vkGetFenceStatus(mDevice.getHandle(), mFence);
        }

    private:
        const Device& mDevice;
        VkFence mFence{ VK_NULL_HANDLE };

    };
}
