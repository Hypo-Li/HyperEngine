#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Fence : public rhi::Fence
    {
    public:
        Fence(const Device& device);

        ~Fence() noexcept override;

        uint64_t getCompletedValue() const override;

        void wait(uint64_t value, uint64_t timeout = std::numeric_limits<uint64_t>::max()) override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkSemaphore getHandle() const noexcept
        {
            return mSemaphore;
        }

    private:
        VkSemaphore mSemaphore = VK_NULL_HANDLE;
    };
}
