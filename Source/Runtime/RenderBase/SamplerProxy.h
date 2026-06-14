#pragma once
#include "RenderProxy.h"
#include "BindlessRegistry.h"
#include "Vulkan/Sampler.h"

namespace hyp
{
    class SamplerProxy : public RenderProxy
    {
    public:
        SamplerProxy(const vk::Sampler::CreateInfo& samplerInfo);

        ~SamplerProxy() noexcept;

        vk::Sampler& getSampler() const noexcept
        {
            return *mSampler;
        }

        BindlessHandle getBindlessHandle() const noexcept
        {
            return mBindlessHandle;
        }

    private:
        TRef<vk::Sampler> mSampler;
        BindlessHandle mBindlessHandle;
    };

    template <>
    struct TProxyTraits<ProxyType::Sampler>
    {
        using Proxy = SamplerProxy;
    };
}
