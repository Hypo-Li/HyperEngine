#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Event : public Refable
    {
    public:
        Event(const Device& device)
            : mDevice(device)
        {
            VkEventCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
            vkCreateEvent(mDevice.getHandle(), &createInfo, nullptr, &mEvent);
        }

        ~Event() noexcept
        {
            vkDestroyEvent(mDevice.getHandle(), mEvent, nullptr);
        }

        VkEvent getHandle() const noexcept
        {
            return mEvent;
        }

        void set() const noexcept
        {
            vkSetEvent(mDevice.getHandle(), mEvent);
        }

        void reset() const noexcept
        {
            vkResetEvent(mDevice.getHandle(), mEvent);
        }

        VkResult getStatus() const noexcept
        {
            return vkGetEventStatus(mDevice.getHandle(), mEvent);
        }

    private:
        const Device& mDevice;
        VkEvent mEvent{ VK_NULL_HANDLE };
    };
}
