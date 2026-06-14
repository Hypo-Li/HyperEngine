#include "Sampler.h"

#include <Core/Common/Hash.h>

namespace hyp::vk
{
    bool Sampler::CreateInfo::operator==(const CreateInfo& other) const noexcept
    {
        return magFilter == other.magFilter &&
            minFilter == other.minFilter &&
            mipmapMode == other.mipmapMode &&
            addressModeU == other.addressModeU &&
            addressModeV == other.addressModeV &&
            addressModeW == other.addressModeW &&
            mipLodBias == other.mipLodBias &&
            anisotropyEnable == other.anisotropyEnable &&
            maxAnisotropy == other.maxAnisotropy &&
            compareEnable == other.compareEnable &&
            compareOp == other.compareOp &&
            minLod == other.minLod &&
            maxLod == other.maxLod &&
            borderColor == other.borderColor &&
            unnormalizedCoordinates == other.unnormalizedCoordinates;
    }

    std::size_t Sampler::CreateInfoHash::operator()(const CreateInfo& info) const noexcept
    {
        size_t seed = 0;
        Hash::combine(seed, info.magFilter);
        Hash::combine(seed, info.minFilter);
        Hash::combine(seed, info.mipmapMode);
        Hash::combine(seed, info.addressModeU);
        Hash::combine(seed, info.addressModeV);
        Hash::combine(seed, info.addressModeW);
        Hash::combine(seed, info.mipLodBias);
        Hash::combine(seed, info.anisotropyEnable);
        Hash::combine(seed, info.maxAnisotropy);
        Hash::combine(seed, info.compareEnable);
        Hash::combine(seed, info.compareOp);
        Hash::combine(seed, info.minLod);
        Hash::combine(seed, info.maxLod);
        Hash::combine(seed, info.borderColor);
        Hash::combine(seed, info.unnormalizedCoordinates);
        return seed;
    }

    Sampler::Sampler(const Device& device, const CreateInfo& createInfo) noexcept
        : mDevice(device)
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = createInfo.magFilter;
        samplerInfo.minFilter = createInfo.minFilter;
        samplerInfo.mipmapMode = createInfo.mipmapMode;
        samplerInfo.addressModeU = createInfo.addressModeU;
        samplerInfo.addressModeV = createInfo.addressModeV;
        samplerInfo.addressModeW = createInfo.addressModeW;
        samplerInfo.mipLodBias = createInfo.mipLodBias;
        samplerInfo.anisotropyEnable = createInfo.anisotropyEnable;
        samplerInfo.maxAnisotropy = createInfo.maxAnisotropy;
        samplerInfo.compareEnable = createInfo.compareEnable;
        samplerInfo.compareOp = createInfo.compareOp;
        samplerInfo.minLod = createInfo.minLod;
        samplerInfo.maxLod = createInfo.maxLod;
        samplerInfo.borderColor = createInfo.borderColor;
        samplerInfo.unnormalizedCoordinates = createInfo.unnormalizedCoordinates;

        vkCreateSampler(mDevice.getHandle(), &samplerInfo, nullptr, &mSampler);
    }

    Sampler::~Sampler() noexcept
    {
        vkDestroySampler(mDevice.getHandle(), mSampler, nullptr);
    }

    SamplerCache::SamplerCache(const Device& device) noexcept
        : mDevice(device)
    {
    }

    Sampler* SamplerCache::get(const Sampler::CreateInfo& createInfo)
    {
        auto find = mSamplerMap.find(createInfo);
        if (find != mSamplerMap.end())
        {
            return find->second.get();
        }
        TRef<Sampler> newSampler = new Sampler(mDevice, createInfo);
        mSamplerMap.emplace(createInfo, newSampler);
        return newSampler.get();
    }

    Sampler* SamplerCache::getLinearClampNoMip()
    {
        if (!mLinearClampNoMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            mLinearClampNoMip = get(samplerInfo);
        }
        return mLinearClampNoMip.get();
    }

    Sampler* SamplerCache::getPointClampNoMip()
    {
        if (!mPointClampNoMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            mPointClampNoMip = get(samplerInfo);
        }
        return mPointClampNoMip.get();
    }

    Sampler* SamplerCache::getLinearRepeatNoMip()
    {
        if (!mLinearRepeatNoMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            mLinearRepeatNoMip = get(samplerInfo);
        }
        return mLinearRepeatNoMip.get();
    }

    Sampler* SamplerCache::getPointRepeatNoMip()
    {
        if (!mPointRepeatNoMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            mPointRepeatNoMip = get(samplerInfo);
        }
        return mPointRepeatNoMip.get();
    }

    Sampler* SamplerCache::getLinearClampMip()
    {
        if (!mLinearClampMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            mLinearClampMip = get(samplerInfo);
        }
        return mLinearClampMip.get();
    }

    Sampler* SamplerCache::getPointClampMip()
    {
        if (!mPointClampMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            mPointClampMip = get(samplerInfo);
        }
        return mPointClampMip.get();
    }

    Sampler* SamplerCache::getLinearRepeatMip()
    {
        if (!mLinearRepeatMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            mLinearRepeatMip = get(samplerInfo);
        }
        return mLinearRepeatMip.get();
    }

    Sampler* SamplerCache::getPointRepeatMip()
    {
        if (!mPointRepeatMip)
        {
            Sampler::CreateInfo samplerInfo;
            samplerInfo.magFilter = VK_FILTER_NEAREST;
            samplerInfo.minFilter = VK_FILTER_NEAREST;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            mPointRepeatMip = get(samplerInfo);
        }
        return mPointRepeatMip.get();
    }

}
