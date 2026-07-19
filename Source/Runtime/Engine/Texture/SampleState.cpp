#include "SampleState.h"
#include "../Context.h"

#include <Core/Common/Hash.h>
#include <RenderBase/SamplerCache.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/SamplerProxy.h>

namespace hyp
{
    template <> Type* Reflection::createType<Filter>()
    {
        auto e = new TEnumeration<Filter>("Filter");
        e->addValue("Point", Filter::Point);
        e->addValue("Bilinear", Filter::Bilinear);
        e->addValue("Trilinear", Filter::Trilinear);
        e->addValue("Anisotropic", Filter::Anisotropic);
        return e;
    }

    template <> Type* Reflection::createType<AddressMode>()
    {
        auto e = new TEnumeration<AddressMode>("AddressMode");
        e->addValue("Repeat", AddressMode::Repeat);
        e->addValue("Clamp", AddressMode::Clamp);
        e->addValue("Mirror", AddressMode::Mirror);
        return e;
    }

    bool SampleState::operator==(const SampleState& other) const noexcept
    {
        return filter == other.filter &&
            addressModeU == other.addressModeU &&
            addressModeV == other.addressModeV &&
            addressModeW == other.addressModeW;
    }

    template <> Type* Reflection::createType<SampleState>()
    {
        auto s = new TStructure<SampleState>("SampleState");
        s->addProperty("Filter", &SampleState::filter);
        s->addProperty("Address Mode U", &SampleState::addressModeU);
        s->addProperty("Address Mode V", &SampleState::addressModeV);
        s->addProperty("Address Mode W", &SampleState::addressModeW);
        return s;
    }

    size_t SampleStateHash::operator()(const SampleState& key) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, key.filter);
        Hash::combine(seed, key.addressModeU);
        Hash::combine(seed, key.addressModeV);
        Hash::combine(seed, key.addressModeW);
        return seed;
    }

    SampleStateManager::~SampleStateManager()
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        std::vector<SamplerProxyHandle> proxyHandles;
        proxyHandles.reserve(mSamplerProxyHandleMap.size());
        for (auto [sampleState, proxyHandle] : mSamplerProxyHandleMap)
        {
            proxyHandles.push_back(proxyHandle);
            renderFrontend.freeProxyHandle(proxyHandle);
        }
        mSamplerProxyHandleMap.clear();

        renderFrontend.enqueueLambda(
            [proxyHandles = std::move(proxyHandles)](FrameContext& context)
            {
                auto& proxyManager = context.getProxyManager();
                for (auto proxyHandle : proxyHandles)
                {
                    proxyManager.destroyProxy(proxyHandle);
                }
            }
        );
    }

    SamplerProxyHandle SampleStateManager::getOrCreateSamplerProxy(const SampleState& sampleState)
    {
        auto it = mSamplerProxyHandleMap.find(sampleState);
        if (it != mSamplerProxyHandleMap.end())
        {
            return it->second;
        }
        SamplerProxyHandle proxyHandle = createSamplerProxy(sampleState);
        mSamplerProxyHandleMap[sampleState] = proxyHandle;
        return proxyHandle;
    }

    static rhi::AddressMode toRHIAddressMode(AddressMode addressMode) noexcept
    {
        switch (addressMode)
        {
        case AddressMode::Repeat: return rhi::AddressMode::Repeat;
        case AddressMode::Clamp: return rhi::AddressMode::Clamp_To_Edge;
        case AddressMode::Mirror: return rhi::AddressMode::Mirrored_Repeat;
        default: return rhi::AddressMode::Repeat;
        }
    }

    SamplerProxyHandle SampleStateManager::createSamplerProxy(const SampleState& sampleState)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        SamplerProxyHandle proxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Sampler>();

        rhi::SamplerDesc samplerDesc;
        switch (sampleState.filter)
        {
        case Filter::Point:
        {
            samplerDesc.minFilter = rhi::Filter::Nearest;
            samplerDesc.magFilter = rhi::Filter::Nearest;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            break;
        }
        case Filter::Bilinear:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            break;
        }
        case Filter::Trilinear:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            break;
        }
        case Filter::Anisotropic:
        {
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            samplerDesc.anisotropyEnable = true;
            samplerDesc.maxAnisotropy = 16.0f;
            break;
        }
        default:
        {
            assert(false && "Invalid Filter");
            break;
        }
        }

        samplerDesc.addressModeU = toRHIAddressMode(sampleState.addressModeU);
        samplerDesc.addressModeV = toRHIAddressMode(sampleState.addressModeV);
        samplerDesc.addressModeW = toRHIAddressMode(sampleState.addressModeW);

        renderFrontend.enqueueLambda(
            [proxyHandle, samplerDesc](FrameContext& context)
            {
                context.getProxyManager().createProxy(proxyHandle, samplerDesc);
            }
        );

        return proxyHandle;
    }
}
