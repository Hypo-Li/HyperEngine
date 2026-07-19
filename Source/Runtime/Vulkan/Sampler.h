#pragma once
#include "Device.h"

#include <RHI/Sampler.h>

namespace hyp::vk
{
    class Sampler : public rhi::Sampler
    {
    public:
        Sampler(const Device& device, const rhi::SamplerDesc& desc);

        ~Sampler() noexcept override;

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkSampler getHandle() const noexcept
        {
            return mSampler;
        }

    private:
        VkSampler mSampler = VK_NULL_HANDLE;
    };
}
