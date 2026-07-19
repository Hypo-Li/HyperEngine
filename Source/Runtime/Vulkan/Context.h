#pragma once
#include "Device.h"

#include <RHI/Context.h>

namespace hyp::vk
{
    class Context : public rhi::Context
    {
    public:
        Context(const rhi::ContextDesc& desc);

        ~Context();

        VkInstance getInstance() const noexcept
        {
            return mInstance;
        }

        VkPhysicalDevice getPhysicalDevice() const noexcept
        {
            return mPhysicalDevice;
        }

    private:
        void createDebugUtilsMessenger();

    private:
        VkInstance mInstance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    };
}
