#pragma once
#include <RHI/BindGroup.h>
#include <RHI/Device.h>
#include <Core/Common/Refable.h>

#include <span>
#include <unordered_map>
#include <vector>

namespace hyp
{
    class BindGroupLayoutCache
    {
    public:
        BindGroupLayoutCache(const rhi::Device& device);

        TRef<rhi::BindGroupLayout> getOrCreate(std::span<const rhi::BindGroupLayoutEntry> entries);

    private:
        struct Key
        {
            std::vector<rhi::BindGroupLayoutEntry> entries;

            bool operator==(const Key& other) const noexcept;
        };

        struct KeyHash
        {
            size_t operator()(const Key& key) const noexcept;
        };

    private:
        const rhi::Device& mDevice;
        std::unordered_map<Key, TRef<rhi::BindGroupLayout>, KeyHash> mCache;
    };
}
