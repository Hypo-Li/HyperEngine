#pragma once
#include "RenderProxy.h"
#include "BindlessRegistry.h"

#include <RHI/Sampler.h>

namespace hyp
{
    class SamplerProxy : public RenderProxy
    {
    public:
        SamplerProxy(const rhi::SamplerDesc& desc);

        ~SamplerProxy() noexcept;

        rhi::Sampler& getSampler() const noexcept
        {
            return *mSampler;
        }

        BindlessHandle getBindlessHandle() const noexcept
        {
            return mBindlessHandle;
        }

    private:
        TRef<rhi::Sampler> mSampler;
        BindlessHandle mBindlessHandle;
    };

    template <>
    struct TProxyTraits<ProxyType::Sampler>
    {
        using Proxy = SamplerProxy;
    };
}
