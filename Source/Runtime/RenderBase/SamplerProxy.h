#pragma once
#include "BindlessRegistry.h"
#include "ProxyHandle.h"
#include "ResourceProxy.h"

#include <RHI/Sampler.h>

#include <unordered_map>

namespace hyp
{
    class RenderFrontend;

    class SamplerProxy : public ResourceProxy
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

    class SamplerProxyCache
    {
    public:
        explicit SamplerProxyCache(RenderFrontend& renderFrontend) noexcept;

        ~SamplerProxyCache();

        SamplerProxyHandle getOrCreate(const rhi::SamplerDesc& desc);

        void shutdown();

    private:
        struct Key
        {
            rhi::SamplerDesc desc;

            bool operator==(const Key& other) const noexcept;
        };

        struct KeyHash
        {
            size_t operator()(const Key& key) const noexcept;
        };

    private:
        RenderFrontend& mRenderFrontend;
        std::unordered_map<Key, SamplerProxyHandle, KeyHash> mCache;
        bool mShutdown = false;
    };
}
