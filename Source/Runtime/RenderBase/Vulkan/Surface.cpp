#include "Surface.h"

namespace hyp::vk
{
    Surface::Surface(const Instance& instance) noexcept
        : mInstance(instance)
    {
    }

    Surface::~Surface() noexcept
    {
        vkDestroySurfaceKHR(mInstance.getHandle(), mSurface, nullptr);
    }

    VkExtent2D Surface::queryCurrentExtent(const PhysicalDevice& physicalDevice) const noexcept
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice.getHandle(), mSurface, &capabilities);
        return capabilities.currentExtent;
    }

}
