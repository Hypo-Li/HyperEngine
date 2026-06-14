#pragma once
#include "Instance.h"
#include "PhysicalDevice.h"

namespace hyp::vk
{
    class Surface : public Refable
    {
    public:
        Surface(const Instance& instance) noexcept;
        virtual ~Surface() noexcept;

        VkSurfaceKHR getHandle() const noexcept
        {
            return mSurface;
        }

        VkExtent2D queryCurrentExtent(const PhysicalDevice& physicalDevice) const noexcept;

    protected:
        const Instance& mInstance;
        VkSurfaceKHR mSurface;
    };
}
