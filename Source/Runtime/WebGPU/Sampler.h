#pragma once
#include "Device.h"

#include <RHI/Sampler.h>

namespace hyp::wgpu
{
    class Sampler : public rhi::Sampler
    {
    public:
        Sampler(const Device& device, const rhi::SamplerDesc& desc);

        ~Sampler() noexcept override;

        const Device& getWgpuDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        WGPUSampler getHandle() const noexcept
        {
            return mSampler;
        }

    private:
        WGPUSampler mSampler = nullptr;
    };
}
