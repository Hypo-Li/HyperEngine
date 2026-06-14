#pragma once
#include "Device.h"

namespace hyp::vk
{
    class Semaphore : public Refable
    {
    public:
        Semaphore(const Device& device, VkSemaphoreType type) noexcept
            : mDevice(device), mSemaphore(VK_NULL_HANDLE), mType(type)
        {
            
        }

        virtual ~Semaphore() noexcept
        {
            if (mSemaphore)
            {
                vkDestroySemaphore(mDevice.getHandle(), mSemaphore, nullptr);
            }
        }

        const Device& getDevice() const noexcept
        {
            return mDevice;
        }

        VkSemaphore getHandle() const noexcept
        {
            return mSemaphore;
        }

        VkSemaphoreType getType() const noexcept
        {
            return mType;
        }

    protected:
        void init(const VkSemaphoreCreateInfo& createInfo)
        {
            vkCreateSemaphore(mDevice.getHandle(), &createInfo, nullptr, &mSemaphore);
        }

    private:
        const Device& mDevice;
        VkSemaphore mSemaphore{ VK_NULL_HANDLE };
        VkSemaphoreType mType;
    };

    class BinarySemaphore : public Semaphore
    {
    public:
        BinarySemaphore(const Device& device) noexcept
            : Semaphore(device, VK_SEMAPHORE_TYPE_BINARY)
        {
            VkSemaphoreCreateInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            init(semaphoreInfo);
        }
    };

    class TimelineSemaphore : public Semaphore
    {
    public:
        TimelineSemaphore(const Device& device, uint64_t initialValue = 0) noexcept
            : Semaphore(device, VK_SEMAPHORE_TYPE_TIMELINE)
        {
            VkSemaphoreTypeCreateInfo timelineInfo = {};
            timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
            timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            timelineInfo.initialValue = initialValue;
            VkSemaphoreCreateInfo semaphoreInfo = {};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreInfo.pNext = &timelineInfo;
            init(semaphoreInfo);
        }

        uint64_t getCounterValue() const noexcept
        {
            uint64_t value;
            vkGetSemaphoreCounterValue(getDevice().getHandle(), getHandle(), &value);
            return value;
        }

        void wait(uint64_t value, uint64_t timeout = UINT64_MAX) const noexcept
        {
            VkSemaphore handle = getHandle();
            VkSemaphoreWaitInfo waitInfo = {};
            waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            waitInfo.semaphoreCount = 1;
            waitInfo.pSemaphores = &handle;
            waitInfo.pValues = &value;
            vkWaitSemaphores(getDevice().getHandle(), &waitInfo, timeout);
        }

        void signal(uint64_t value) const noexcept
        {
            VkSemaphoreSignalInfo signalInfo = {};
            signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
            signalInfo.semaphore = getHandle();
            signalInfo.value = value;
            vkSignalSemaphore(getDevice().getHandle(), &signalInfo);
        }
    };
}
