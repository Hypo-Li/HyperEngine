#pragma once
#include "Device.h"
#include "Memory.h"

namespace hyp::vk
{
    class Context
    {
    public:
        static Context& get();

        Instance& getInstance() const
        {
            return *mInstance;
        }

        PhysicalDevice& getPhysicalDevice() const
        {
            return *mPhysicalDevice;
        }

        Device& getDevice() const
        {
            return *mDevice;
        }

    private:
        Context();
        ~Context();

    private:
        TRef<Instance> mInstance;
        TRef<PhysicalDevice> mPhysicalDevice;
        TRef<Device> mDevice;
    };
}
