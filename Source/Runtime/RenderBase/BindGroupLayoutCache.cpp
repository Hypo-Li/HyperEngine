#include "BindGroupLayoutCache.h"
#include <Core/Common/Hash.h>

namespace hyp
{
    BindGroupLayoutCache::BindGroupLayoutCache(const rhi::Device& device)
        : mDevice(device)
    {
    }

    bool BindGroupLayoutCache::Key::operator==(const Key& other) const noexcept
    {
        if (entries.size() != other.entries.size())
            return false;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (!(entries[i] == other.entries[i]))
                return false;
        }

        return true;
    }

    TRef<rhi::BindGroupLayout> BindGroupLayoutCache::getOrCreate(std::span<const rhi::BindGroupLayoutEntry> entries)
    {
        Key key;
        key.entries.assign(entries.begin(), entries.end());

        auto it = mCache.find(key);
        if (it != mCache.end())
            return it->second;

        TRef<rhi::BindGroupLayout> layout = mDevice.createBindGroupLayout(entries);
        mCache.emplace(std::move(key), layout);
        return layout;
    }

    size_t BindGroupLayoutCache::KeyHash::operator()(const Key& key) const noexcept
    {
        size_t seed = 0;
        for (const auto& entry : key.entries)
        {
            Hash::combine(seed, entry.binding);
            Hash::combine(seed, entry.type);
            Hash::combine(seed, entry.access);
            Hash::combine(seed, entry.count);
            Hash::combine(seed, entry.textureViewType);
            Hash::combine(seed, entry.storageTextureFormat);
            Hash::combine(seed, entry.textureSampleType);
            Hash::combine(seed, entry.multiSample);
            Hash::combine(seed, entry.samplerBindingType);
            Hash::combine(seed, entry.stageFlags);
            Hash::combine(seed, entry.bindless);
        }
        return seed;
    }
}
