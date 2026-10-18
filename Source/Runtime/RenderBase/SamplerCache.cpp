#include "SamplerCache.h"
#include <Core/Common/Hash.h>

namespace hyp
{
    SamplerCache::SamplerCache(const rhi::Device& device)
        : mDevice(device)
    {
    }

    bool SamplerCache::Key::operator==(const Key& other) const noexcept
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

    TRef<rhi::Sampler> SamplerCache::getOrCreate(const rhi::SamplerDesc& desc)
    {
        Key key{ desc };

        auto it = mCache.find(key);
        if (it != mCache.end())
            return it->second;

        TRef<rhi::Sampler> sampler = mDevice.createSampler(desc);
        mCache.emplace(std::move(key), sampler);
        return sampler;
    }

    TRef<rhi::Sampler> SamplerCache::getLinearClampNoMip()
    {
        if (!mLinearClampNoMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            samplerDesc.addressModeU = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeV = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeW = rhi::AddressMode::Clamp_To_Edge;
            mLinearClampNoMip = getOrCreate(samplerDesc);
        }
        return mLinearClampNoMip;
    }

    TRef<rhi::Sampler> SamplerCache::getPointClampNoMip()
    {
        if (!mPointClampNoMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Nearest;
            samplerDesc.minFilter = rhi::Filter::Nearest;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            samplerDesc.addressModeU = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeV = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeW = rhi::AddressMode::Clamp_To_Edge;
            mPointClampNoMip = getOrCreate(samplerDesc);
        }
        return mPointClampNoMip;
    }

    TRef<rhi::Sampler> SamplerCache::getLinearRepeatNoMip()
    {
        if (!mLinearRepeatNoMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            samplerDesc.addressModeU = rhi::AddressMode::Repeat;
            samplerDesc.addressModeV = rhi::AddressMode::Repeat;
            samplerDesc.addressModeW = rhi::AddressMode::Repeat;
            mLinearRepeatNoMip = getOrCreate(samplerDesc);
        }
        return mLinearRepeatNoMip;
    }

    TRef<rhi::Sampler> SamplerCache::getPointRepeatNoMip()
    {
        if (!mPointRepeatNoMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Nearest;
            samplerDesc.minFilter = rhi::Filter::Nearest;
            samplerDesc.mipmapMode = rhi::MipmapMode::Nearest;
            samplerDesc.addressModeU = rhi::AddressMode::Repeat;
            samplerDesc.addressModeV = rhi::AddressMode::Repeat;
            samplerDesc.addressModeW = rhi::AddressMode::Repeat;
            mPointRepeatNoMip = getOrCreate(samplerDesc);
        }
        return mPointRepeatNoMip;
    }

    TRef<rhi::Sampler> SamplerCache::getLinearClampMip()
    {
        if (!mLinearClampMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            samplerDesc.addressModeU = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeV = rhi::AddressMode::Clamp_To_Edge;
            samplerDesc.addressModeW = rhi::AddressMode::Clamp_To_Edge;
            mLinearClampMip = getOrCreate(samplerDesc);
        }
        return mLinearClampMip;
    }

    TRef<rhi::Sampler> SamplerCache::getLinearRepeatMip()
    {
        if (!mLinearRepeatMip)
        {
            rhi::SamplerDesc samplerDesc;
            samplerDesc.magFilter = rhi::Filter::Linear;
            samplerDesc.minFilter = rhi::Filter::Linear;
            samplerDesc.mipmapMode = rhi::MipmapMode::Linear;
            samplerDesc.addressModeU = rhi::AddressMode::Repeat;
            samplerDesc.addressModeV = rhi::AddressMode::Repeat;
            samplerDesc.addressModeW = rhi::AddressMode::Repeat;
            mLinearRepeatMip = getOrCreate(samplerDesc);
        }
        return mLinearRepeatMip;
    }

    size_t SamplerCache::KeyHash::operator()(const Key& key) const noexcept
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
