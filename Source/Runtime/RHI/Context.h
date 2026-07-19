#pragma once
#include "Device.h"

namespace hyp::rhi
{
    enum class Backend
    {
        Vulkan,
        WebGPU,
    };

    struct ContextDesc
    {
        Backend backend;
        FeatureRequirements featureRequirements;
        SwapchainDesc swapchainDesc;
        bool enableValidation = false;
    };

    class Context
    {
    public:
        Context(Backend backend) : mBackend(backend) {}

        virtual ~Context() = default;

        const Device& getDevice() const noexcept
        {
            return *mDevice;
        }

        Backend getBackend() const noexcept
        {
            return mBackend;
        }

    protected:
        std::unique_ptr<Device> mDevice;
        Backend mBackend;
    };
}
