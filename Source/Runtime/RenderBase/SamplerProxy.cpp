#include "SamplerProxy.h"

#include "FrameContext.h"
#include "ProxyManager.h"
#include "RenderBackend.h"
#include "RenderFrontend.h"

#include <Core/Common/Hash.h>

#include <cassert>
#include <utility>
#include <vector>

namespace hyp
{
    SamplerProxy::SamplerProxy(const rhi::SamplerDesc& desc)
    {
        auto& renderBackend = getRenderBackend();
        auto& device = renderBackend.getDevice();
        mSampler = renderBackend.getSamplerCache().getOrCreate(desc);

        if (device.getFeatures().bindless)
        {
            auto& bindlessRegistry = renderBackend.getBindlessRegistry();
            mBindlessHandle = bindlessRegistry.registerSampler(mSampler);
        }
    }

    SamplerProxy::~SamplerProxy() noexcept
    {
        auto& renderBackend = getRenderBackend();
        bool bindless = renderBackend.getDevice().getFeatures().bindless;

        renderBackend.getDeferredTaskQueue().push(
            [sampler = std::move(mSampler),
            bindlessRegistry = bindless ? &renderBackend.getBindlessRegistry() : nullptr,
            bindlessHandle = std::move(mBindlessHandle)]() mutable
            {
                if (bindlessRegistry && bindlessHandle.isValid())
                    bindlessRegistry->unregister(bindlessHandle);
                sampler = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    SamplerProxyCache::SamplerProxyCache(RenderFrontend& renderFrontend) noexcept :
        mRenderFrontend(renderFrontend)
    {
    }

    SamplerProxyCache::~SamplerProxyCache()
    {
        shutdown();
    }

    SamplerProxyHandle SamplerProxyCache::getOrCreate(const rhi::SamplerDesc& desc)
    {
        assert(!mShutdown && "SamplerProxyCache cannot create proxies after shutdown");
        if (mShutdown)
            return {};

        Key key{ desc };
        auto it = mCache.find(key);
        if (it != mCache.end())
            return it->second;

        SamplerProxyHandle proxyHandle =
            mRenderFrontend.allocateProxyHandle<ProxyType::Sampler>();
        mRenderFrontend.enqueueLambda(
            [proxyHandle, desc](FrameContext& context)
            {
                context.getProxyManager().createProxy(proxyHandle, desc);
            }
        );

        mCache.emplace(std::move(key), proxyHandle);
        return proxyHandle;
    }

    void SamplerProxyCache::shutdown()
    {
        if (mShutdown)
            return;
        mShutdown = true;

        std::vector<SamplerProxyHandle> proxyHandles;
        proxyHandles.reserve(mCache.size());
        for (const auto& [key, proxyHandle] : mCache)
        {
            proxyHandles.push_back(proxyHandle);
            mRenderFrontend.freeProxyHandle(proxyHandle);
        }
        mCache.clear();

        if (proxyHandles.empty())
            return;

        mRenderFrontend.enqueueLambda(
            [proxyHandles = std::move(proxyHandles)](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                for (SamplerProxyHandle proxyHandle : proxyHandles)
                    proxyManager.destroyProxy(proxyHandle);
            }
        );
    }

    bool SamplerProxyCache::Key::operator==(const Key& other) const noexcept
    {
        return desc.minFilter == other.desc.minFilter &&
            desc.magFilter == other.desc.magFilter &&
            desc.mipmapMode == other.desc.mipmapMode &&
            desc.addressModeU == other.desc.addressModeU &&
            desc.addressModeV == other.desc.addressModeV &&
            desc.addressModeW == other.desc.addressModeW &&
            desc.mipLodBias == other.desc.mipLodBias &&
            desc.anisotropyEnable == other.desc.anisotropyEnable &&
            desc.maxAnisotropy == other.desc.maxAnisotropy &&
            desc.compareEnable == other.desc.compareEnable &&
            desc.compareOp == other.desc.compareOp &&
            desc.minLod == other.desc.minLod &&
            desc.maxLod == other.desc.maxLod;
    }

    size_t SamplerProxyCache::KeyHash::operator()(const Key& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.desc.minFilter);
        Hash::combine(seed, key.desc.magFilter);
        Hash::combine(seed, key.desc.mipmapMode);
        Hash::combine(seed, key.desc.addressModeU);
        Hash::combine(seed, key.desc.addressModeV);
        Hash::combine(seed, key.desc.addressModeW);
        Hash::combine(seed, key.desc.mipLodBias);
        Hash::combine(seed, key.desc.anisotropyEnable);
        Hash::combine(seed, key.desc.maxAnisotropy);
        Hash::combine(seed, key.desc.compareEnable);
        Hash::combine(seed, key.desc.compareOp);
        Hash::combine(seed, key.desc.minLod);
        Hash::combine(seed, key.desc.maxLod);
        return seed;
    }
}
