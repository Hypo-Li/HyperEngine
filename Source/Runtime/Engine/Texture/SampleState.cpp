#include "SampleState.h"
#include "../Context.h"

#include <Core/Common/Hash.h>
#include <RenderBase/Vulkan/Sampler.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/SamplerProxy.h>

namespace hyp
{
    bool SampleState::operator==(const SampleState& other) const noexcept
    {
        return filter == other.filter &&
            addressModeU == other.addressModeU &&
            addressModeV == other.addressModeV &&
            addressModeW == other.addressModeW;
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
            [proxyHandles = std::move(proxyHandles)](RenderFrameContext& context)
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

    static VkSamplerAddressMode toVkSamplerAddressMode(AddressMode addressMode) noexcept
    {
        switch (addressMode)
        {
        case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case AddressMode::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case AddressMode::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    }

    SamplerProxyHandle SampleStateManager::createSamplerProxy(const SampleState& sampleState)
    {
        auto& renderFrontend = Context::get().getRenderFrontend();
        SamplerProxyHandle proxyHandle = renderFrontend.allocateProxyHandle<ProxyType::Sampler>();

        vk::Sampler::CreateInfo samplerInfo;
        switch (sampleState.filter)
        {
        case Filter::Point:
        {
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        }
        case Filter::Bilinear:
        {
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        }
        case Filter::Trilinear:
        {
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        }
        case Filter::Anisotropic:
        {
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.anisotropyEnable = VK_TRUE;
            samplerInfo.maxAnisotropy = 16.0f;
            break;
        }
        default:
        {
            assert(false && "Invalid Filter");
            break;
        }
        }

        samplerInfo.addressModeU = toVkSamplerAddressMode(sampleState.addressModeU);
        samplerInfo.addressModeV = toVkSamplerAddressMode(sampleState.addressModeV);
        samplerInfo.addressModeW = toVkSamplerAddressMode(sampleState.addressModeW);

        renderFrontend.enqueueLambda(
            [proxyHandle, samplerInfo](RenderFrameContext& context)
            {
                context.getProxyManager().createProxy(proxyHandle, samplerInfo);
            }
        );

        return proxyHandle;
    }

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

    template <> Type* Reflection::createType<SampleState>()
    {
        auto s = new TStructure<SampleState>("SampleState");
        s->addProperty("Filter", &SampleState::filter);
        s->addProperty("Address Mode U", &SampleState::addressModeU);
        s->addProperty("Address Mode V", &SampleState::addressModeV);
        s->addProperty("Address Mode W", &SampleState::addressModeW);
        return s;
    }
}
